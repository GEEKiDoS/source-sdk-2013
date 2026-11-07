// Game-owned native103E2780 seven-argument sphere overload and103E3920
// triangle adaptation. Register this ONE client TU; use existing tier2's
// InitializeStandardMaterials and material-overload RenderTriangle. No cache.
#include "cbase.h"
#include "tier2/renderutils.h"
#include "tier1/callqueue.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "tier0/memdbgon.h"

// Existing GLOBAL tier2/renderutils.cpp owner, already linked by tier2.
// Declaration only: no new prebuilt export or independent material lifetime.
extern void InitializeStandardMaterials();
extern "C" void L4D2_GlowNormalize( Vector *direction );

// Own stack-only numeric transport to x64 helpers, not an entity layout.
// The first azimuth consumes native unspilled x87 horizontal radius; later
// azimuths consume its F32 spill. Ten-byte x87 storage preserves that value.
struct ScrimmageSphereRow_t
{
	float horizontal;
	float z;
	unsigned char firstHorizontal[10];
};
extern "C" void L4D2_ScrimmageSphereRow( float radius, float row, float phiDenominator, ScrimmageSphereRow_t *result );
extern "C" void L4D2_ScrimmageSphereVertex( const ScrimmageSphereRow_t *row, int column, float thetaDenominator, Vector *position );

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

void RenderSphere( const Vector &center, float radius, int nTheta, int nPhi,
	Color color, IMaterial *material, bool insideOut )
{
	InitializeStandardMaterials();
	CMatRenderContextPtr context( materials );
	ICallQueue *queue = context->GetCallQueue();
	if ( queue )
	{
		// Actual SDK value-copy functor ownership: Vector/Color by value,
		// IMaterial borrowed pointer, bool copied; no extra refcount/reset.
		typedef void ( *SphereCall_t )( const Vector &, float, int, int, Color, IMaterial *, bool );
		queue->QueueCall( static_cast< SphereCall_t >( &RenderSphere ), center,
			radius, nTheta, nPhi, color, material, insideOut );
		return;
	}
	const int triangles = 2 * ( nTheta * ( nPhi - 1 ) );
	const int indices = ( nTheta + 1 ) * ( 2 * nPhi - 2 );
	if ( triangles == 0 )
		return;
	context->Bind( material, NULL );
	IMesh *mesh = context->GetDynamicMesh( true, NULL, NULL, NULL );
	CMeshBuilder builder;
	builder.Begin( mesh, MATERIAL_TRIANGLE_STRIP, triangles, indices );
	const float phiDenominator = static_cast< float >( nPhi - 1 );
	const float oneOverPhi = 1.0f / phiDenominator;
	const float thetaDenominator = static_cast< float >( nTheta - 1 );
	const float oneOverTheta = 1.0f / thetaDenominator;
	for ( int row = 0; row < nPhi; ++row )
	{
		if ( nTheta > 0 )
		{
			const float rowFloat = static_cast< float >( row );
			const float v = rowFloat * oneOverPhi;
			ScrimmageSphereRow_t polar;
			L4D2_ScrimmageSphereRow( radius, rowFloat, phiDenominator, &polar );
			for ( int column = 0; column < nTheta; ++column )
			{
				Vector position;
				L4D2_ScrimmageSphereVertex( &polar, column, thetaDenominator, &position );
				Vector normal = position;
				L4D2_GlowNormalize( &normal );
				Vector tangent( -position.y, position.x, 0.0f );
				L4D2_GlowNormalize( &tangent );
				const float userData[4] = { tangent.x, tangent.y, tangent.z, 1.0f };
				builder.Position3f( center.x + position.x, center.y + position.y, center.z + position.z );
				builder.Normal3f( normal.x, normal.y, normal.z );
				builder.Color4ub( color.r(), color.g(), color.b(), color.a() );
				builder.UserData( userData );
				builder.TexCoord2f( 0, static_cast< float >( column ) * oneOverTheta, v );
				builder.AdvanceVertex();
			}
		}
	}
	for ( int row = 0; row < nPhi - 1; ++row )
	{
		if ( insideOut )
		{
			// Native reverses azimuth enumeration, not the two indices in
			// each pair. It emits NO row-join degenerates on this arm.
			for ( int column = nTheta - 1; column >= 0; --column )
			{
				const int index = nTheta * row + column;
				builder.Index( static_cast< unsigned short >( nTheta + index ) );
				builder.AdvanceIndex();
				builder.Index( static_cast< unsigned short >( index ) );
				builder.AdvanceIndex();
			}
		}
		else
		{
			int index = 0;
			for ( int column = 0; column < nTheta; ++column )
			{
				index = nTheta * row + column;
				builder.Index( static_cast< unsigned short >( nTheta + index ) );
				builder.AdvanceIndex();
				builder.Index( static_cast< unsigned short >( index ) );
				builder.AdvanceIndex();
			}
			if ( row < nPhi - 2 )
			{
				builder.Index( static_cast< unsigned short >( index ) );
				builder.AdvanceIndex();
				builder.Index( static_cast< unsigned short >( nTheta + index + 1 ) );
				builder.AdvanceIndex();
			}
		}
	}
	builder.End();
	mesh->Draw();
}

void RenderScrimmageTriangleNative( const Vector &a, const Vector &b,
	const Vector &c, Color color, bool depthTest )
{
	//103E3920 initializes before material choice. The target bool overload
	// chooses first, then its material overload initializes again; this call
	// supplies the missing first initialization using the SAME real owner.
	InitializeStandardMaterials();
	RenderTriangle( a, b, c, color, depthTest );
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
