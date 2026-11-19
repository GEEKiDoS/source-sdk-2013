#ifndef SSR_RAY_HLSLI
#define SSR_RAY_HLSLI

// Clip the ray to the D3D clip volume before rasterizing in screen space. The bounds are linear in the world-ray parameter.
bool SSR_ClipSegment( float4 c0, float4 c1, out float s0, out float s1 )
{
	s0 = 0.0;
	s1 = 1.0;
	float4 d = c1 - c0;
	float a[7] = { c0.w - 1e-4, c0.w - c0.x, c0.w + c0.x, c0.w - c0.y, c0.w + c0.y, c0.z, c0.w - c0.z };
	float b[7] = { d.w, d.w - d.x, d.w + d.x, d.w - d.y, d.w + d.y, d.z, d.w - d.z };
	bool inside = true;
	[unroll] for ( uint i = 0; i < 7; ++i )
	{
		if ( b[i] == 0.0 )
		{
			if ( a[i] < 0.0 ) inside = false;
		}
		else
		{
			float s = -a[i] / b[i];
			if ( b[i] > 0.0 ) s0 = max( s0, s );
			else s1 = min( s1, s );
		}
	}
	return inside && s0 < s1;
}

float2 SSR_ClipToPixel( float4 clip, float2 viewport )
{
	float2 ndc = clip.xy / clip.w;
	return float2( ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5 ) * viewport;
}

#endif
