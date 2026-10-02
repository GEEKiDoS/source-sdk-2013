#include "fixed_function_dx12.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <cstring>

namespace shaderapidx12
{
namespace
{
std::string InputStruct(VertexFormat_t f)
{
    std::ostringstream s; s<<"struct VSIn { float4 pos:POSITION;\n";
    if(f&VERTEX_NORMAL)s<<(f&VERTEX_FORMAT_COMPRESSED?"uint4":"float4")<<" normal:NORMAL;\n";
    if(f&VERTEX_COLOR)s<<"float4 color:COLOR0;\n";
    if(f&VERTEX_SPECULAR)s<<"float4 spec:COLOR1;\n";
    if(NumBoneWeights(f))s<<(f&VERTEX_FORMAT_COMPRESSED?"int2":"float2")<<" weights:BLENDWEIGHT; float4 indices:BLENDINDICES;\n";
    for(int i=0;i<VERTEX_MAX_TEXTURE_COORDINATES;++i)if(TexCoordSize(i,f))s<<"float4 tc"<<i<<":TEXCOORD"<<i<<";\n";
    s<<"};\n"; return s.str();
}
std::string OutputStruct(int count=VERTEX_MAX_TEXTURE_COORDINATES)
{
    std::ostringstream s; s<<"struct VSOut { float4 pos:SV_POSITION; float4 color:COLOR0; float4 spec:COLOR1;\n";
    for(int i=0;i<count;++i)s<<"float4 tc"<<i<<":TEXCOORD"<<i<<";\n";
    s<<"float4 clip0:SV_ClipDistance0; float2 clip1:SV_ClipDistance1;};\n"; return s.str();
}
const char *TexGenExpr(ShaderTexGenParam_t p)
{
    switch(p){
    case SHADER_TEXGENPARAM_EYE_LINEAR:return "mul(M(17),v.pos)";
    case SHADER_TEXGENPARAM_SPHERE_MAP:return "float4(normalize(mul((float3x3)M(17),normal)).xy*0.5+0.5,0,1)";
    case SHADER_TEXGENPARAM_CAMERASPACEREFLECTIONVECTOR:return "float4(reflect(normalize(mul(M(17),v.pos).xyz),normalize(mul((float3x3)M(17),normal))),1)";
    case SHADER_TEXGENPARAM_CAMERASPACENORMAL:return "float4(normalize(mul((float3x3)M(17),normal)),1)";
    default:return nullptr;
    }
}
std::string VertexSource(const FixedFunctionStateDX12 &st)
{
    std::ostringstream s;s<<"cbuffer VSFloat:register(b0){float4 vc[256];}\ncbuffer VSTexture:register(b1){float4 tc[40];}\ncbuffer VSBools:register(b2){uint4 vb[4];}\ncbuffer VSClip:register(b3){float4 clipViewport;float4 clipPoint;float4 clipPlanes[6];float4 clipOffset;float4 clipScale;}\n";
    s<<InputStruct(st.format)<<OutputStruct()<<"float4x4 M(uint n){return float4x4(vc[n],vc[n+1],vc[n+2],vc[n+3]);}\nVSOut main(VSIn v){VSOut o=(VSOut)0; float4 p=float4(v.pos.xyz,1);\n";
    if(st.format&VERTEX_NORMAL){
        if(st.format&VERTEX_FORMAT_COMPRESSED)s<<"float2 packed=float2(v.normal.xy);float2 zSign=(packed<128);float2 xyAbs=abs(packed-128)-zSign;float2 xySign=(xyAbs<64);float2 xy=(abs(xyAbs-64)-xySign)/63;float3 normal=normalize(float3(xy,1-xy.x-xy.y));normal*=float3(1-2*xySign,1-2*zSign.x);\n";
        else s<<"float3 normal=v.normal.xyz;\n";
    }
    const bool skinned=NumBoneWeights(st.format)&&st.vertexBlend;
    if(skinned)s<<"float4 bp=0;float3 bn=0;float3 w=float3("<<(st.format&VERTEX_FORMAT_COMPRESSED?"float2(v.weights)/32767.0":"v.weights")<<",1-v.weights.x"<<(st.format&VERTEX_FORMAT_COMPRESSED?"/32767.0":"")<<"-v.weights.y"<<(st.format&VERTEX_FORMAT_COMPRESSED?"/32767.0":"")<<");[unroll]for(int j=0;j<3;++j){uint bone=(uint)(v.indices[j]*255+0.5);uint base=58+bone*3;float4x4 skin=float4x4(vc[base],vc[base+1],vc[base+2],float4(0,0,0,1));bp+=mul(skin,p)*w[j];"<<(st.format&VERTEX_NORMAL?"bn+=mul((float3x3)skin,normal)*w[j];":"")<<"}p=bp;\n";
    s<<"o.pos=mul(M("<<(skinned?8:4)<<"),p);o.color=";
    const bool color1=(st.format&VERTEX_COLOR)&&(st.drawFlags&SHADER_DRAW_COLOR),color2=st.format&VERTEX_SPECULAR;
    if(st.materialSource==SHADER_MATERIALSOURCE_COLOR2&&color2)s<<"v.spec";
    else if((st.materialSource==SHADER_MATERIALSOURCE_COLOR1||!st.lighting)&&color1)s<<"v.color";
    else s<<"float4(1,1,1,1)";
    s<<";o.spec="<<(color2?"v.spec":"float4(0,0,0,1)")<<";\n";
    s<<"o.clip0=float4(dot(o.pos,clipPlanes[0]),dot(o.pos,clipPlanes[1]),dot(o.pos,clipPlanes[2]),dot(o.pos,clipPlanes[3]));o.clip1=float2(dot(o.pos,clipPlanes[4]),dot(o.pos,clipPlanes[5]));\n";
    if(st.lighting&&(st.format&VERTEX_NORMAL)){
        s<<"float3 n=normalize("<<(skinned?"bn":"mul((float3x3)M(58),normal)")<<");float3 wp="<<(skinned?"p.xyz":"mul(float4x4(vc[58],vc[59],vc[60],float4(0,0,0,1)),p).xyz")<<";\n";
        s<<"float3 lit=vc[1].rgb+vc[n.x>=0?21:22].rgb*(n.x*n.x)+vc[n.y>=0?23:24].rgb*(n.y*n.y)+vc[n.z>=0?25:26].rgb*(n.z*n.z);float3 specular=0;\n";
        s<<"[unroll]for(int li=0;li<4;++li){if(vb[0][li]){float3 lc=vc[27+li*5].xyz;float3 ld=vc[28+li*5].xyz;float3 lp=vc[29+li*5].xyz;float3 toLight=lp-wp;float dist=length(toLight);float3 L=vc[27+li*5].w>0.5?normalize(-ld):toLight/max(dist,1e-6);float3 atten=vc[31+li*5].xyz;float falloff=vc[27+li*5].w>0.5?1:1/max(dot(atten,float3(1,dist,dist*dist)),1e-5);float diffuse=max(dot(n,L),0);lit+=lc*diffuse*falloff;";
        if(st.specular)s<<"float3 V=normalize(vc[2].xyz-wp);specular+=lc*pow(max(dot(n,normalize(L+V)),0),16)*falloff;";
        s<<"}}o.color.rgb*=lit;";
        if(st.specular)s<<"o.spec.rgb+=specular;";
        s<<"\n";
    }
    for(int i=0;i<VERTEX_MAX_TEXTURE_COORDINATES;++i){
        int coord=i%VERTEX_MAX_TEXTURE_COORDINATES;
        if(i<4){if(st.drawFlags&(SHADER_DRAW_TEXCOORD0<<i))coord=0;else if(st.drawFlags&(SHADER_DRAW_LIGHTMAP_TEXCOORD0<<i))coord=1;else if(st.drawFlags&(SHADER_DRAW_SECONDARY_TEXCOORD0<<i))coord=2;}
        if(st.texgen[i]&&(st.texgenParam[i]==SHADER_TEXGENPARAM_EYE_LINEAR||(st.format&VERTEX_NORMAL))&&st.texgenParam[i]!=SHADER_TEXGENPARAM_OBJECT_LINEAR)s<<"o.tc"<<i<<"="<<TexGenExpr(st.texgenParam[i])<<";\n";
        else if(TexCoordSize(coord,st.format))s<<"o.tc"<<i<<"=v.tc"<<coord<<";\n";
        else s<<"o.tc"<<i<<"=float4(0,0,0,1);\n";
        if(i<8)s<<"if(tc["<<(32+i)<<"].x>0.5){o.tc"<<i<<"=mul(float4x4(tc["<<(i*4)<<"],tc["<<(i*4+1)<<"],tc["<<(i*4+2)<<"],tc["<<(i*4+3)<<"]),o.tc"<<i<<");if(tc["<<(32+i)<<"].y>0.5){float q=(tc["<<(32+i)<<"].z>3.5?o.tc"<<i<<".w:(tc["<<(32+i)<<"].z>2.5?o.tc"<<i<<".z:o.tc"<<i<<".y));o.tc"<<i<<".xyz/=max(abs(q),1e-6)*(q<0?-1:1);}}\n";
    }
    // Generated shaders keep their exact clip position (no D3D9 half-pixel offset); clipViewport.zw carries only the
    // native-AA jitter delta that translated and native shaders receive through clipViewport.xy.
    s<<"o.pos.xy=mad(clipViewport.zw,o.pos.ww,o.pos.xy);\nreturn o;}\n";return s.str();
}
std::string ArgExpr(int stage,ShaderTexArg_t arg)
{
    switch(arg){case SHADER_TEXARG_TEXTURE:return "sampled"+std::to_string(stage);case SHADER_TEXARG_VERTEXCOLOR:return "vertexColor";case SHADER_TEXARG_SPECULARCOLOR:return "specularColor";case SHADER_TEXARG_CONSTANTCOLOR:return "pc[0]";case SHADER_TEXARG_PREVIOUSSTAGE:return "prev";case SHADER_TEXARG_TEXTUREALPHA:return "sampled"+std::to_string(stage)+".aaaa";case SHADER_TEXARG_INVTEXTUREALPHA:return "(1-sampled"+std::to_string(stage)+".aaaa)";case SHADER_TEXARG_ONE:return "float4(1,1,1,1)";default:return "float4(0,0,0,0)";}
}
std::string ApplyOp(ShaderTexOp_t op,const std::string &a,const std::string &b,int stage)
{
    const std::string sampled="sampled"+std::to_string(stage);
    switch(op){case SHADER_TEXOP_MODULATE:return "("+a+"*"+b+")";case SHADER_TEXOP_MODULATE2X:return "("+a+"*"+b+"*2)";case SHADER_TEXOP_MODULATE4X:return "("+a+"*"+b+"*4)";case SHADER_TEXOP_SELECTARG1:return a;case SHADER_TEXOP_SELECTARG2:return b;case SHADER_TEXOP_ADD:return "("+a+"+"+b+")";case SHADER_TEXOP_SUBTRACT:return "("+a+"-"+b+")";case SHADER_TEXOP_ADDSIGNED2X:return "(("+a+"+"+b+"-0.5)*2)";case SHADER_TEXOP_BLEND_CONSTANTALPHA:return "("+a+"*pc[0].a+"+b+"*(1-pc[0].a))";case SHADER_TEXOP_BLEND_TEXTUREALPHA:return "("+a+"*"+sampled+".a+"+b+"*(1-"+sampled+".a))";case SHADER_TEXOP_BLEND_PREVIOUSSTAGEALPHA:return "("+a+"*prev.a+"+b+"*(1-prev.a))";case SHADER_TEXOP_MODULATECOLOR_ADDALPHA:return "float4(("+a+"*"+b+").rgb+("+a+").a,("+a+").a)";case SHADER_TEXOP_MODULATEINVCOLOR_ADDALPHA:return "float4(((1-"+a+")*"+b+").rgb+("+a+").a,("+a+").a)";case SHADER_TEXOP_DOTPRODUCT3:return "dot(("+a+").rgb*2-1,("+b+").rgb*2-1).xxxx";default:return "prev";}
}
std::string PixelSource(const FixedFunctionStateDX12 &st,const std::vector<ShaderLinkageDX12> *linkedInputs)
{
    std::array<bool,2> hasColor{!linkedInputs,!linkedInputs};std::array<bool,16> hasTexcoord{};hasTexcoord.fill(!linkedInputs);
    if(linkedInputs)for(const auto &link:*linkedInputs){if(link.usage==10&&link.usageIndex<hasColor.size())hasColor[link.usageIndex]=true;else if(link.usage==5&&link.usageIndex<hasTexcoord.size())hasTexcoord[link.usageIndex]=true;}
    std::ostringstream s;s<<"cbuffer PSFloat:register(b0){float4 pc[30];}\n";
    s<<"struct PSIn{float4 pos:SV_POSITION;";
    if(hasColor[0])s<<(st.flatShade?"nointerpolation ":"")<<"float4 color:COLOR0;";
    if(hasColor[1])s<<"float4 spec:COLOR1;";
    for(int i=0;i<16;++i)if(hasTexcoord[i])s<<"float4 tc"<<i<<":TEXCOORD"<<i<<";";
    s<<"};\n";
    for(int i=0;i<16;++i)if(st.textureEnabled[i])s<<(st.textureTypes[i]==2?"TextureCube":st.textureTypes[i]==3?"Texture3D":"Texture2D")<<" texture"<<i<<":register(t"<<i<<"); SamplerState samp"<<i<<":register(s"<<i<<");\n";
    s<<"float4 main(PSIn i):SV_TARGET{float4 vertexColor="<<(hasColor[0]?"i.color":"float4(1,1,1,1)")<<";float4 specularColor="<<(hasColor[1]?"i.spec":"float4(0,0,0,1)")<<";float4 prev=vertexColor;\n";
    if(st.constantColor)s<<"prev.rgb*=pc[0].rgb;\n";
    if(st.alphaPipe){s<<"prev.a=1;\n";if(st.vertexAlpha&&(st.drawFlags&SHADER_DRAW_COLOR))s<<"prev.a*=vertexColor.a;\n";if(st.constantAlpha)s<<"prev.a*=pc[0].a;\n";}
    const int stages=st.customPipe?std::clamp(st.texCoordCount,0,16):16;
    for(int stage=0;stage<stages;++stage){
        if(!st.textureEnabled[stage]&&!st.customPipe&&!st.textureAlpha[stage])continue;
        if(st.textureEnabled[stage]){
            const int coordinate=hasTexcoord[stage]?stage:stage%VERTEX_MAX_TEXTURE_COORDINATES;
            s<<"float4 uv"<<stage<<"="<<(hasTexcoord[coordinate]?"i.tc"+std::to_string(coordinate):"float4(0,0,0,1)")<<";\n";
            s<<"float4 sampled"<<stage<<"=texture"<<stage<<".Sample(samp"<<stage<<",uv"<<stage<<(st.textureTypes[stage]==1?".xy":".xyz")<<");\n";
        }else s<<"float4 sampled"<<stage<<"=float4(1,1,1,1);\n";
        if(st.customPipe){
            if(st.colorOp[stage]!=SHADER_TEXOP_DISABLE)s<<"prev.rgb=("<<ApplyOp(st.colorOp[stage],ArgExpr(stage,st.colorArg1[stage]),ArgExpr(stage,st.colorArg2[stage]),stage)<<").rgb;\n";
            if(st.alphaOp[stage]!=SHADER_TEXOP_DISABLE)s<<"prev.a=("<<ApplyOp(st.alphaOp[stage],ArgExpr(stage,st.alphaArg1[stage]),ArgExpr(stage,st.alphaArg2[stage]),stage)<<").a;\n";
        }else if(st.textureEnabled[stage]){
            const float overbright=st.overbright[stage]<2.f?1.f:(st.overbright[stage]<4.f?2.f:4.f);
            s<<"prev.rgb*=sampled"<<stage<<".rgb*"<<overbright<<";\n";
            if(!st.alphaPipe||st.textureAlpha[stage])s<<"prev.a*=sampled"<<stage<<".a;\n";
        }
        s<<"prev=saturate(prev);\n";
    }
    if(st.alphaTest){
        s<<"float alpha8=floor(saturate(prev.a)*255+0.5); float ref8=floor(saturate(pc[1].a)*255+1e-5);\n";
        switch(st.alphaFunction){case SHADER_ALPHAFUNC_NEVER:s<<"clip(-1);\n";break;case SHADER_ALPHAFUNC_LESS:s<<"clip(ref8-alpha8-0.5);\n";break;case SHADER_ALPHAFUNC_EQUAL:s<<"clip(0.5-abs(alpha8-ref8));\n";break;case SHADER_ALPHAFUNC_LEQUAL:s<<"clip(ref8-alpha8+0.5);\n";break;case SHADER_ALPHAFUNC_GREATER:s<<"clip(alpha8-ref8-0.5);\n";break;case SHADER_ALPHAFUNC_NOTEQUAL:s<<"clip(abs(alpha8-ref8)-0.5);\n";break;case SHADER_ALPHAFUNC_GEQUAL:s<<"clip(alpha8-ref8+0.5);\n";break;default:break;}
    }
    if(st.specular)s<<"prev.rgb+=specularColor.rgb;\n";
    if(st.fogMode!=SHADER_FOGMODE_DISABLED)s<<"float ndcDepth=i.pos.z;float eyeDepth=abs((pc[2].y-ndcDepth*pc[2].w)/max(abs(ndcDepth*pc[2].z-pc[2].x),1e-6));float fogFactor=max(1-pc[28].w,saturate((pc[28].y-eyeDepth)*pc[28].z));prev.rgb=lerp(pc[29].rgb,prev.rgb,fogFactor);\n";
    s<<"return saturate(prev);}\n";return s.str();
}
} // anonymous namespace
ShaderRecordDX12 *CompileNativeShaderRecordDX12(CShaderDeviceDX12 *device,const std::string &src,bool pixel,const char *profile)
{
    if(!device||!profile)return nullptr;
    IShaderBuffer *buffer=device->CompileShader(src.data(),src.size(),profile);
    if(!buffer)return nullptr;
    auto record=std::make_unique<ShaderRecordDX12>();static std::atomic<uint64_t> next{0x100000000ull};record->identity=next.fetch_add(1);record->stagePixel=pixel;
    const unsigned char *bits=static_cast<const unsigned char *>(buffer->GetBits());record->bytecode.assign(bits,bits+buffer->GetSize());buffer->Release();
    if(!pixel){Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
        if(SUCCEEDED(D3DReflect(record->bytecode.data(),record->bytecode.size(),IID_PPV_ARGS(&reflection)))){
            D3D11_SHADER_DESC desc{};
            if(SUCCEEDED(reflection->GetDesc(&desc)))for(UINT i=0;i<desc.OutputParameters;++i){
                D3D11_SIGNATURE_PARAMETER_DESC p{};if(FAILED(reflection->GetOutputParameterDesc(i,&p)))continue;
                uint32_t usage=5;
                if(!_stricmp(p.SemanticName,"POSITION")||!_stricmp(p.SemanticName,"SV_POSITION"))usage=0;
                else if(!_stricmp(p.SemanticName,"COLOR"))usage=10;
                else if(_stricmp(p.SemanticName,"TEXCOORD"))continue;
                record->translated.outputLinkage.push_back({usage,p.SemanticIndex,p.Register,p.Mask,false});
            }
        }
    }
    return record.release();
}
ShaderRecordDX12 *CreateFixedFunctionShaderDX12(CShaderDeviceDX12 *device,const FixedFunctionStateDX12 &state,bool pixel,const std::vector<ShaderLinkageDX12> *linkedInputs){return CompileNativeShaderRecordDX12(device,pixel?PixelSource(state,linkedInputs):VertexSource(state),pixel,pixel?"ps_5_0":"vs_5_0");}
} // namespace shaderapidx12
