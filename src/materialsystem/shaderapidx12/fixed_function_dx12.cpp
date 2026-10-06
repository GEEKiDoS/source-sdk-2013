//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Generated SM5 replacement shaders for legacy fixed-function snapshots.
//
//=============================================================================//

#include "fixed_function_dx12.h"
#include "tier0/threadtools.h"
#include "tier1/utlbuffer.h"
#include "tier1/strtools.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include "../stdshaders_dx12/generated/inc/highres_lightmaps_hlsl.inc"
#include "../stdshaders_dx12/generated/inc/shadowmap_lighting_hlsl.inc"

namespace shaderapidx12
{
namespace
{
//-----------------------------------------------------------------------------
// Purpose: Emits the VS input struct for a Source vertex format
//-----------------------------------------------------------------------------
void InputStruct( CUtlBuffer &buf, VertexFormat_t format )
{
	buf.PutString( "struct VSIn { float4 pos:POSITION;\n" );
	if ( format & VERTEX_NORMAL )
		buf.Printf( "%s normal:NORMAL;\n", ( format & VERTEX_FORMAT_COMPRESSED ) ? "uint4" : "float4" );
	if ( format & VERTEX_COLOR )
		buf.PutString( "float4 color:COLOR0;\n" );
	if ( format & VERTEX_SPECULAR )
		buf.PutString( "float4 spec:COLOR1;\n" );
	if ( NumBoneWeights( format ) )
		buf.Printf( "%s weights:BLENDWEIGHT; float4 indices:BLENDINDICES;\n", ( format & VERTEX_FORMAT_COMPRESSED ) ? "int2" : "float2" );
	for ( int i = 0; i < VERTEX_MAX_TEXTURE_COORDINATES; ++i )
		if ( TexCoordSize( i, format ) )
			buf.Printf( "float4 tc%d:TEXCOORD%d;\n", i, i );
	buf.PutString( "};\n" );
}

//-----------------------------------------------------------------------------
// Purpose: Emits the VS output struct (all texcoords, two clip-distance registers)
//-----------------------------------------------------------------------------
void OutputStruct( CUtlBuffer &buf, bool highres )
{
	buf.PutString( "struct VSOut { float4 pos:SV_POSITION; float4 color:COLOR0; float4 spec:COLOR1;\n" );
	for ( int i = 0; i < VERTEX_MAX_TEXTURE_COORDINATES; ++i )
		buf.Printf( "float4 tc%d:TEXCOORD%d;\n", i, i );
	if ( highres )
		buf.PutString( "float4 tc13:TEXCOORD13;float4 tc14:TEXCOORD14;float4 tc15:TEXCOORD15;\n" );
	buf.PutString( "float4 clip0:SV_ClipDistance0; float2 clip1:SV_ClipDistance1;};\n" );
}

//-----------------------------------------------------------------------------
// Purpose: HLSL texture-coordinate generation expression
//-----------------------------------------------------------------------------
const char *TexGenExpr( ShaderTexGenParam_t param )
{
	switch ( param )
	{
	case SHADER_TEXGENPARAM_EYE_LINEAR:
		return "mul(M(17),v.pos)";
	case SHADER_TEXGENPARAM_SPHERE_MAP:
		return "float4(normalize(mul((float3x3)M(17),normal)).xy*0.5+0.5,0,1)";
	case SHADER_TEXGENPARAM_CAMERASPACEREFLECTIONVECTOR:
		return "float4(reflect(normalize(mul(M(17),v.pos).xyz),normalize(mul((float3x3)M(17),normal))),1)";
	case SHADER_TEXGENPARAM_CAMERASPACENORMAL:
		return "float4(normalize(mul((float3x3)M(17),normal)),1)";
	default:
		return nullptr;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Emits the fixed-function vertex shader
//-----------------------------------------------------------------------------
void VertexSource( CUtlBuffer &buf, const FixedFunctionStateDX12 &state )
{
	buf.PutString( "cbuffer VSFloat:register(b0){float4 vc[256];}\ncbuffer VSTexture:register(b1){float4 tc[40];}\ncbuffer VSBools:register(b2){uint4 vb[4];}\ncbuffer VSClip:register(b3){float4 clipViewport;float4 clipPoint;float4 clipPlanes[6];float4 clipOffset;float4 clipScale;}\n" );
	InputStruct( buf, state.format );
	OutputStruct( buf, state.highresSamplerMask != 0 );
	buf.PutString( "float4x4 M(uint n){return float4x4(vc[n],vc[n+1],vc[n+2],vc[n+3]);}\nVSOut main(VSIn v){VSOut o=(VSOut)0; float4 p=float4(v.pos.xyz,1);\n" );
	const bool bCompressed = ( state.format & VERTEX_FORMAT_COMPRESSED ) != 0;
	if ( state.format & VERTEX_NORMAL )
	{
		if ( bCompressed )
			buf.PutString( "float2 packed=float2(v.normal.xy);float2 zSign=(packed<128);float2 xyAbs=abs(packed-128)-zSign;float2 xySign=(xyAbs<64);float2 xy=(abs(xyAbs-64)-xySign)/63;float3 normal=normalize(float3(xy,1-xy.x-xy.y));normal*=float3(1-2*xySign,1-2*zSign.x);\n" );
		else
			buf.PutString( "float3 normal=v.normal.xyz;\n" );
	}
	const bool bSkinned = NumBoneWeights( state.format ) && state.vertexBlend;
	if ( bSkinned )
	{
		buf.Printf( "float4 bp=0;float3 bn=0;float3 w=float3(%s,1-v.weights.x%s-v.weights.y%s);[unroll]for(int j=0;j<3;++j){uint bone=(uint)(v.indices[j]*255+0.5);uint base=58+bone*3;float4x4 skin=float4x4(vc[base],vc[base+1],vc[base+2],float4(0,0,0,1));bp+=mul(skin,p)*w[j];%s}p=bp;\n",
		    bCompressed ? "float2(v.weights)/32767.0" : "v.weights",
		    bCompressed ? "/32767.0" : "",
		    bCompressed ? "/32767.0" : "",
		    ( state.format & VERTEX_NORMAL ) ? "bn+=mul((float3x3)skin,normal)*w[j];" : "" );
	}
	buf.Printf( "o.pos=mul(M(%d),p);o.color=", bSkinned ? 8 : 4 );
	const bool bColor1 = ( state.format & VERTEX_COLOR ) && ( state.drawFlags & SHADER_DRAW_COLOR );
	const bool bColor2 = ( state.format & VERTEX_SPECULAR ) != 0;
	if ( state.materialSource == SHADER_MATERIALSOURCE_COLOR2 && bColor2 )
		buf.PutString( "v.spec" );
	else if ( ( state.materialSource == SHADER_MATERIALSOURCE_COLOR1 || !state.lighting ) && bColor1 )
		buf.PutString( "v.color" );
	else
		buf.PutString( "float4(1,1,1,1)" );
	buf.Printf( ";o.spec=%s;\n", bColor2 ? "v.spec" : "float4(0,0,0,1)" );
	buf.PutString( "o.clip0=float4(dot(o.pos,clipPlanes[0]),dot(o.pos,clipPlanes[1]),dot(o.pos,clipPlanes[2]),dot(o.pos,clipPlanes[3]));o.clip1=float2(dot(o.pos,clipPlanes[4]),dot(o.pos,clipPlanes[5]));\n" );
	if ( state.lighting && ( state.format & VERTEX_NORMAL ) )
	{
		buf.Printf( "float3 n=normalize(%s);float3 wp=%s;\n",
		    bSkinned ? "bn" : "mul((float3x3)M(58),normal)",
		    bSkinned ? "p.xyz" : "mul(float4x4(vc[58],vc[59],vc[60],float4(0,0,0,1)),p).xyz" );
		buf.PutString( "float3 lit=vc[1].rgb+vc[n.x>=0?21:22].rgb*(n.x*n.x)+vc[n.y>=0?23:24].rgb*(n.y*n.y)+vc[n.z>=0?25:26].rgb*(n.z*n.z);float3 specular=0;\n" );
		buf.PutString( "[unroll]for(int li=0;li<4;++li){if(vb[0][li]){float3 lc=vc[27+li*5].xyz;float3 ld=vc[28+li*5].xyz;float3 lp=vc[29+li*5].xyz;float3 toLight=lp-wp;float dist=length(toLight);float3 L=vc[27+li*5].w>0.5?normalize(-ld):toLight/max(dist,1e-6);float3 atten=vc[31+li*5].xyz;float falloff=vc[27+li*5].w>0.5?1:1/max(dot(atten,float3(1,dist,dist*dist)),1e-5);float diffuse=max(dot(n,L),0);lit+=lc*diffuse*falloff;" );
		if ( state.specular )
			buf.PutString( "float3 V=normalize(vc[2].xyz-wp);specular+=lc*pow(max(dot(n,normalize(L+V)),0),16)*falloff;" );
		buf.PutString( "}}o.color.rgb*=lit;" );
		if ( state.specular )
			buf.PutString( "o.spec.rgb+=specular;" );
		buf.PutString( "\n" );
	}
	for ( int i = 0; i < VERTEX_MAX_TEXTURE_COORDINATES; ++i )
	{
		int nCoord = i % VERTEX_MAX_TEXTURE_COORDINATES;
		if ( i < 4 )
		{
			if ( state.drawFlags & ( SHADER_DRAW_TEXCOORD0 << i ) )
				nCoord = 0;
			else if ( state.drawFlags & ( SHADER_DRAW_LIGHTMAP_TEXCOORD0 << i ) )
				nCoord = 1;
			else if ( state.drawFlags & ( SHADER_DRAW_SECONDARY_TEXCOORD0 << i ) )
				nCoord = 2;
		}
		if ( state.texgen[i] && ( state.texgenParam[i] == SHADER_TEXGENPARAM_EYE_LINEAR || ( state.format & VERTEX_NORMAL ) ) && state.texgenParam[i] != SHADER_TEXGENPARAM_OBJECT_LINEAR )
			buf.Printf( "o.tc%d=%s;\n", i, TexGenExpr( state.texgenParam[i] ) );
		else if ( TexCoordSize( nCoord, state.format ) )
			buf.Printf( "o.tc%d=v.tc%d;\n", i, nCoord );
		else
			buf.Printf( "o.tc%d=float4(0,0,0,1);\n", i );
		if ( i < 8 )
		{
			const int nFlags = 32 + i;
			buf.Printf( "if(tc[%d].x>0.5){o.tc%d=mul(float4x4(tc[%d],tc[%d],tc[%d],tc[%d]),o.tc%d);if(tc[%d].y>0.5){float q=(tc[%d].z>3.5?o.tc%d.w:(tc[%d].z>2.5?o.tc%d.z:o.tc%d.y));o.tc%d.xyz/=max(abs(q),1e-6)*(q<0?-1:1);}}\n",
			    nFlags, i, i * 4, i * 4 + 1, i * 4 + 2, i * 4 + 3, i, nFlags, nFlags, i, nFlags, i, i, i );
		}
	}
	if ( state.highresSamplerMask && TexCoordSize( 1, state.format ) )
		buf.PutString( "o.tc15=v.tc1;\n" ); // Original BASE UV before transforms/bumped offsets.
	if ( state.highresSamplerMask )
	{
		buf.Printf( "o.tc13=float4(%s,1);\n", bSkinned ? "p.xyz" : "mul(float4x4(vc[58],vc[59],vc[60],float4(0,0,0,1)),p).xyz" );
		if ( state.format & VERTEX_NORMAL )
			buf.Printf( "o.tc14=float4(normalize(%s),0);\n", bSkinned ? "bn" : "mul((float3x3)M(58),normal)" );
	}
	// Generated shaders keep their exact clip position (no D3D9 half-pixel offset); clipViewport.zw carries only the
	// native-AA jitter delta that translated and native shaders receive through clipViewport.xy.
	buf.PutString( "o.pos.xy=mad(clipViewport.zw,o.pos.ww,o.pos.xy);\nreturn o;}\n" );
}

//-----------------------------------------------------------------------------
// Purpose: HLSL expression for a texture-stage argument; formatted results go to szExpr
//-----------------------------------------------------------------------------
const char *ArgExpr( int nStage, ShaderTexArg_t arg, char ( &szExpr )[32] )
{
	switch ( arg )
	{
	case SHADER_TEXARG_TEXTURE:
		V_snprintf( szExpr, sizeof( szExpr ), "sampled%d", nStage );
		return szExpr;
	case SHADER_TEXARG_VERTEXCOLOR:
		return "vertexColor";
	case SHADER_TEXARG_SPECULARCOLOR:
		return "specularColor";
	case SHADER_TEXARG_CONSTANTCOLOR:
		return "pc[0]";
	case SHADER_TEXARG_PREVIOUSSTAGE:
		return "prev";
	case SHADER_TEXARG_TEXTUREALPHA:
		V_snprintf( szExpr, sizeof( szExpr ), "sampled%d.aaaa", nStage );
		return szExpr;
	case SHADER_TEXARG_INVTEXTUREALPHA:
		V_snprintf( szExpr, sizeof( szExpr ), "(1-sampled%d.aaaa)", nStage );
		return szExpr;
	case SHADER_TEXARG_ONE:
		return "float4(1,1,1,1)";
	default:
		return "float4(0,0,0,0)";
	}
}

// Reuse the authored texture operation for its linear selected-light contribution;
// native alpha is shared, so lighting never changes blend/alpha operands.
const char *CombinedArgExpr( int nStage, ShaderTexArg_t arg, char ( &expr )[128] )
{
	if ( arg == SHADER_TEXARG_TEXTURE )
	{
		V_snprintf( expr, sizeof( expr ), "float4(sampled%d.rgb+sampledDirect%d,sampled%d.a)", nStage, nStage, nStage );
		return expr;
	}
	if ( arg == SHADER_TEXARG_PREVIOUSSTAGE )
		return "float4(prev.rgb+fixedDirectRGB,prev.a)";
	char ordinary[32];
	V_strncpy( expr, ArgExpr( nStage, arg, ordinary ), sizeof( expr ) );
	return expr;
}

//-----------------------------------------------------------------------------
// Purpose: Emits the HLSL expression for a texture-stage operation
//-----------------------------------------------------------------------------
void ApplyOp( CUtlBuffer &buf, ShaderTexOp_t op, const char *pszA, const char *pszB, int nStage, bool combined = false )
{
	switch ( op )
	{
	case SHADER_TEXOP_MODULATE:
		buf.Printf( "(%s*%s)", pszA, pszB );
		break;
	case SHADER_TEXOP_MODULATE2X:
		buf.Printf( "(%s*%s*2)", pszA, pszB );
		break;
	case SHADER_TEXOP_MODULATE4X:
		buf.Printf( "(%s*%s*4)", pszA, pszB );
		break;
	case SHADER_TEXOP_SELECTARG1:
		buf.PutString( pszA );
		break;
	case SHADER_TEXOP_SELECTARG2:
		buf.PutString( pszB );
		break;
	case SHADER_TEXOP_ADD:
		buf.Printf( "(%s+%s)", pszA, pszB );
		break;
	case SHADER_TEXOP_SUBTRACT:
		buf.Printf( "(%s-%s)", pszA, pszB );
		break;
	case SHADER_TEXOP_ADDSIGNED2X:
		buf.Printf( "((%s+%s-0.5)*2)", pszA, pszB );
		break;
	case SHADER_TEXOP_BLEND_CONSTANTALPHA:
		buf.Printf( "(%s*pc[0].a+%s*(1-pc[0].a))", pszA, pszB );
		break;
	case SHADER_TEXOP_BLEND_TEXTUREALPHA:
		buf.Printf( "(%s*sampled%d.a+%s*(1-sampled%d.a))", pszA, nStage, pszB, nStage );
		break;
	case SHADER_TEXOP_BLEND_PREVIOUSSTAGEALPHA:
		buf.Printf( "(%s*prev.a+%s*(1-prev.a))", pszA, pszB );
		break;
	case SHADER_TEXOP_MODULATECOLOR_ADDALPHA:
		buf.Printf( "float4((%s*%s).rgb+(%s).a,(%s).a)", pszA, pszB, pszA, pszA );
		break;
	case SHADER_TEXOP_MODULATEINVCOLOR_ADDALPHA:
		buf.Printf( "float4(((1-%s)*%s).rgb+(%s).a,(%s).a)", pszA, pszB, pszA, pszA );
		break;
	case SHADER_TEXOP_DOTPRODUCT3:
		buf.Printf( "dot((%s).rgb*2-1,(%s).rgb*2-1).xxxx", pszA, pszB );
		break;
	default:
		buf.PutString( combined ? "float4(prev.rgb+fixedDirectRGB,prev.a)" : "prev" );
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Emits the fixed-function pixel shader; pLinkedInputs restricts the
//          interpolants to those the bound vertex shader writes
//-----------------------------------------------------------------------------
void PixelSource( CUtlBuffer &buf, const FixedFunctionStateDX12 &state, const CUtlVector<ShaderLinkageDX12> *pLinkedInputs )
{
	bool bHasColor[2] = { !pLinkedInputs, !pLinkedInputs };
	bool bHasTexcoord[16];
	for ( int i = 0; i < ARRAYSIZE( bHasTexcoord ); ++i )
		bHasTexcoord[i] = !pLinkedInputs;
	if ( pLinkedInputs )
	{
		for ( int i = 0; i < pLinkedInputs->Count(); ++i )
		{
			const ShaderLinkageDX12 &link = ( *pLinkedInputs )[i];
			if ( link.usage == 10 && link.usageIndex < ARRAYSIZE( bHasColor ) )
				bHasColor[link.usageIndex] = true;
			else if ( link.usage == 5 && link.usageIndex < ARRAYSIZE( bHasTexcoord ) )
				bHasTexcoord[link.usageIndex] = true;
		}
	}
	if ( state.highresSamplerMask )
	{
		buf.PutString( kHighresLightmapsHlsl );
		buf.PutString( kShadowmapLightingHlsl );
	}
	buf.PutString( "cbuffer PSFloat:register(b0){float4 pc[30];}\n" );
	buf.PutString( "struct PSIn{float4 pos:SV_POSITION;" );
	if ( bHasColor[0] )
		buf.Printf( "%sfloat4 color:COLOR0;", state.flatShade ? "nointerpolation " : "" );
	if ( bHasColor[1] )
		buf.PutString( "float4 spec:COLOR1;" );
	for ( int i = 0; i < 16; ++i )
		if ( bHasTexcoord[i] )
			buf.Printf( "float4 tc%d:TEXCOORD%d;", i, i );
	if ( state.highresSamplerMask )
		buf.PutString( "bool front:SV_IsFrontFace;" );
	buf.PutString( "};\n" );
	for ( int i = 0; i < 16; ++i )
	{
		if ( !state.textureEnabled[i] )
			continue;
		const char *pszTextureType = state.textureTypes[i] == 2 ? "TextureCube" : state.textureTypes[i] == 3 ? "Texture3D" :
		                                                                                                       "Texture2D";
		buf.Printf( "%s texture%d:register(t%d); SamplerState samp%d:register(s%d);\n", pszTextureType, i, i, i, i );
	}
	buf.Printf( "float4 main(PSIn i):SV_TARGET{float4 vertexColor=%s;float4 specularColor=%s;float4 prev=vertexColor;\n",
	    bHasColor[0] ? "i.color" : "float4(1,1,1,1)", bHasColor[1] ? "i.spec" : "float4(0,0,0,1)" );
	if ( state.highresSamplerMask )
		buf.PutString( "HlightReceiver hlr=HighresLightmap_Begin(i.tc15.xy);"
			"float3 highresNormal=dot(i.tc14.xyz,i.tc14.xyz)>1e-12?normalize(i.tc14.xyz):normalize(cross(ddx(i.tc13.xyz),ddy(i.tc13.xyz)))*(i.front?1:-1);"
			"ShadowMapReceiver smr=ShadowMap_BeginReceiver(i.tc13.xyz,highresNormal,i.pos.xy);"
			"smr.bakedSunVisibility=hlr.sun;"
			"ShadowMapDirect fixedDirect=ShadowMap_GatherDirect(smr,ShadowMap_ShadeLambert(highresNormal));"
			"float3 fixedDirectRGB=0;\n" );
	if ( state.constantColor )
		buf.PutString( "prev.rgb*=pc[0].rgb;\n" );
	if ( state.alphaPipe )
	{
		buf.PutString( "prev.a=1;\n" );
		if ( state.vertexAlpha && ( state.drawFlags & SHADER_DRAW_COLOR ) )
			buf.PutString( "prev.a*=vertexColor.a;\n" );
		if ( state.constantAlpha )
			buf.PutString( "prev.a*=pc[0].a;\n" );
	}
	const int nStages = state.customPipe ? clamp( state.texCoordCount, 0, 16 ) : 16;
	for ( int nStage = 0; nStage < nStages; ++nStage )
	{
		if ( !state.textureEnabled[nStage] && !state.customPipe && !state.textureAlpha[nStage] )
			continue;
		if ( state.textureEnabled[nStage] )
		{
			const int nCoordinate = bHasTexcoord[nStage] ? nStage : nStage % VERTEX_MAX_TEXTURE_COORDINATES;
			if ( bHasTexcoord[nCoordinate] )
				buf.Printf( "float4 uv%d=i.tc%d;\n", nStage, nCoordinate );
			else
				buf.Printf( "float4 uv%d=float4(0,0,0,1);\n", nStage );
			buf.Printf( "float4 sampled%d=texture%d.Sample(samp%d,uv%d%s);\n", nStage, nStage, nStage, nStage, state.textureTypes[nStage] == 1 ? ".xy" : ".xyz" );
			if ( state.highresSamplerMask & ( 1u << nStage ) )
				buf.Printf( "sampled%d=HighresLightmap_Plane(hlr,0,sampled%d);\n", nStage, nStage );
		}
		else
			buf.Printf( "float4 sampled%d=float4(1,1,1,1);\n", nStage );
		if ( state.highresSamplerMask )
			buf.Printf( "float3 sampledDirect%d=%s;\n", nStage, ( state.highresSamplerMask & ( 1u << nStage ) ) ?
				"fixedDirect.diffuse*hlr.valid" : "float3(0,0,0)" );
		if ( state.customPipe )
		{
			char szArg1[32], szArg2[32];
			if ( state.colorOp[nStage] != SHADER_TEXOP_DISABLE )
			{
				if ( state.highresSamplerMask )
				{
					char combinedArg1[128], combinedArg2[128];
					buf.Printf( "float3 nativeStage%d=(", nStage );
					ApplyOp( buf, state.colorOp[nStage], ArgExpr( nStage, state.colorArg1[nStage], szArg1 ), ArgExpr( nStage, state.colorArg2[nStage], szArg2 ), nStage );
					buf.PutString( ").rgb;\nfixedDirectRGB=(" );
					ApplyOp( buf, state.colorOp[nStage], CombinedArgExpr( nStage, state.colorArg1[nStage], combinedArg1 ), CombinedArgExpr( nStage, state.colorArg2[nStage], combinedArg2 ), nStage, true );
					buf.Printf( ").rgb-nativeStage%d;prev.rgb=nativeStage%d;\n", nStage, nStage );
				}
				else
				{
					buf.PutString( "prev.rgb=(" );
					ApplyOp( buf, state.colorOp[nStage], ArgExpr( nStage, state.colorArg1[nStage], szArg1 ), ArgExpr( nStage, state.colorArg2[nStage], szArg2 ), nStage );
					buf.PutString( ").rgb;\n" );
				}
			}
			if ( state.alphaOp[nStage] != SHADER_TEXOP_DISABLE )
			{
				buf.PutString( "prev.a=(" );
				ApplyOp( buf, state.alphaOp[nStage], ArgExpr( nStage, state.alphaArg1[nStage], szArg1 ), ArgExpr( nStage, state.alphaArg2[nStage], szArg2 ), nStage );
				buf.PutString( ").a;\n" );
			}
		}
		else if ( state.textureEnabled[nStage] )
		{
			const float flOverbright = state.overbright[nStage] < 2.f ? 1.f : ( state.overbright[nStage] < 4.f ? 2.f : 4.f );
			if ( state.highresSamplerMask )
			{
				buf.Printf( "fixedDirectRGB=fixedDirectRGB*sampled%d.rgb+(prev.rgb+fixedDirectRGB)*sampledDirect%d;\n", nStage, nStage );
				buf.Printf( "prev.rgb*=sampled%d.rgb*(cHlightRoute.x!=0?1:%g);\n", nStage, flOverbright );
			}
			else
				buf.Printf( "prev.rgb*=sampled%d.rgb*%g;\n", nStage, flOverbright );
			if ( !state.alphaPipe || state.textureAlpha[nStage] )
				buf.Printf( "prev.a*=sampled%d.a;\n", nStage );
		}
		buf.PutString( state.highresSamplerMask ? "if(cHlightRoute.x!=0)prev.a=saturate(prev.a);else prev=saturate(prev);\n" : "prev=saturate(prev);\n" );
	}
	if ( state.alphaTest )
	{
		buf.PutString( "float alpha8=floor(saturate(prev.a)*255+0.5); float ref8=floor(saturate(pc[1].a)*255+1e-5);\n" );
		switch ( state.alphaFunction )
		{
		case SHADER_ALPHAFUNC_NEVER:
			buf.PutString( "clip(-1);\n" );
			break;
		case SHADER_ALPHAFUNC_LESS:
			buf.PutString( "clip(ref8-alpha8-0.5);\n" );
			break;
		case SHADER_ALPHAFUNC_EQUAL:
			buf.PutString( "clip(0.5-abs(alpha8-ref8));\n" );
			break;
		case SHADER_ALPHAFUNC_LEQUAL:
			buf.PutString( "clip(ref8-alpha8+0.5);\n" );
			break;
		case SHADER_ALPHAFUNC_GREATER:
			buf.PutString( "clip(alpha8-ref8-0.5);\n" );
			break;
		case SHADER_ALPHAFUNC_NOTEQUAL:
			buf.PutString( "clip(abs(alpha8-ref8)-0.5);\n" );
			break;
		case SHADER_ALPHAFUNC_GEQUAL:
			buf.PutString( "clip(alpha8-ref8+0.5);\n" );
			break;
		default:
			break;
		}
	}
	if ( state.highresSamplerMask )
		buf.PutString( "prev.rgb+=fixedDirectRGB;\n" );
	if ( state.specular )
		buf.PutString( "prev.rgb+=specularColor.rgb;\n" );
	if ( state.fogMode != SHADER_FOGMODE_DISABLED )
		buf.PutString( "float ndcDepth=i.pos.z;float eyeDepth=abs((pc[2].y-ndcDepth*pc[2].w)/max(abs(ndcDepth*pc[2].z-pc[2].x),1e-6));float fogFactor=max(1-pc[28].w,saturate((pc[28].y-eyeDepth)*pc[28].z));prev.rgb=lerp(pc[29].rgb,prev.rgb,fogFactor);\n" );
	buf.PutString( state.highresSamplerMask ? "return HighresLightmap_Finish(cHlightRoute.x!=0?prev:saturate(prev));}\n" : "return saturate(prev);}\n" );
}
} // anonymous namespace

//-----------------------------------------------------------------------------
// Purpose: Compiles HLSL source into a native shader record; vertex records also
//          reflect their output linkage for pixel-shader matching
//-----------------------------------------------------------------------------
ShaderRecordDX12 *CompileNativeShaderRecordDX12( CShaderDeviceDX12 *pDevice, const char *pszSource, bool bPixel, const char *pszProfile )
{
	IShaderBuffer *pBuffer = pDevice->CompileShader( pszSource, V_strlen( pszSource ), pszProfile );
	if ( !pBuffer )
		return nullptr;
	ShaderRecordDX12 *pRecord = new ShaderRecordDX12;
	static CInterlockedIntT<uint64> s_nNextIdentity( 0x100000000ull );
	pRecord->identity = s_nNextIdentity.AtomicAdd( 1 );
	pRecord->stagePixel = bPixel;
	pRecord->bytecode.CopyArray( static_cast<const unsigned char *>( pBuffer->GetBits() ), static_cast<int>( pBuffer->GetSize() ) );
	pBuffer->Release();
	if ( !bPixel )
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> pReflection;
		if ( SUCCEEDED( D3DReflect( pRecord->bytecode.Base(), static_cast<SIZE_T>( pRecord->bytecode.Count() ), IID_PPV_ARGS( &pReflection ) ) ) )
		{
			D3D11_SHADER_DESC desc{};
			if ( SUCCEEDED( pReflection->GetDesc( &desc ) ) )
			{
				for ( UINT i = 0; i < desc.OutputParameters; ++i )
				{
					D3D11_SIGNATURE_PARAMETER_DESC parameter{};
					if ( FAILED( pReflection->GetOutputParameterDesc( i, &parameter ) ) )
						continue;
					uint32_t nUsage = 5;
					if ( !_stricmp( parameter.SemanticName, "POSITION" ) || !_stricmp( parameter.SemanticName, "SV_POSITION" ) )
						nUsage = 0;
					else if ( !_stricmp( parameter.SemanticName, "COLOR" ) )
						nUsage = 10;
					else if ( _stricmp( parameter.SemanticName, "TEXCOORD" ) )
						continue;
					ShaderLinkageDX12 &link = pRecord->translated.outputLinkage[pRecord->translated.outputLinkage.AddToTail()];
					link.usage = nUsage;
					link.usageIndex = parameter.SemanticIndex;
					link.registerIndex = parameter.Register;
					link.writeMask = parameter.Mask;
					link.centroid = false;
				}
			}
		}
	}
	return pRecord;
}

//-----------------------------------------------------------------------------
// Purpose: Generates and compiles the VS or PS for a fixed-function snapshot
//-----------------------------------------------------------------------------
ShaderRecordDX12 *CreateFixedFunctionShaderDX12( CShaderDeviceDX12 *pDevice, const FixedFunctionStateDX12 &state, bool bPixel, const CUtlVector<ShaderLinkageDX12> *pLinkedInputs )
{
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	if ( bPixel )
		PixelSource( buf, state, pLinkedInputs );
	else
		VertexSource( buf, state );
	ShaderRecordDX12 *record = CompileNativeShaderRecordDX12( pDevice, buf.String(), bPixel, bPixel ? "ps_5_0" : "vs_5_0" );
	if ( record )
	{
		record->lightmapSamplerMask = bPixel ? state.highresSamplerMask : 0;
		record->highresAbi = bPixel && state.highresSamplerMask != 0;
		record->lightingAbi = bPixel && state.highresSamplerMask != 0;
		record->samplerRolesReady = true;
	}
	return record;
}
} // namespace shaderapidx12
