#pragma once
#include "materialsystem/shaderapidx12/native_cbuffer_dx12.h"
#include "materialsystem/shaderapidx12/native_engine_cbuffers_dx12.h"
#include "shaderapi/ishaderapi.h"
#include "materialsystem/idebugtextureinfo.h"
#include "shaderapi/ishadershadow.h"
#include "mathlib/lightdesc.h"
#include "materialsystem/shaderapidx12/resources_dx12.h"
#include "materialsystem/shaderapidx12/shaderdevice_dx12.h"
#include "materialsystem/shaderapidx12/vertex_layout_dx12.h"
#include "materialsystem/shaderapidx12/textures_dx12.h"
#include "materialsystem/shaderapidx12/shader_vcs_dx12.h"
#include "materialsystem/shaderapidx12/fixed_function_dx12.h"
#include "materialsystem/shaderapidx12/motion_vectors_dx12.h"
#include "materialsystem/shaderapidx12/mesh_dx12.h"
#include <d3d12.h>
#include <DirectXMath.h>
#include <array>
#include "tier0/threadtools.h"
#include "tier1/utlhashtable.h"
#include "tier1/utlvector.h"
#include "tier1/utlstring.h"
#include <string>
#include <memory>
#include <climits>

#include "materialsystem/shaderapidx12/pipeline_dx12.h"
class IShaderUtil;
class ConVar;
namespace shaderapidx12
{
// Pointer-identity string from a process-lifetime intern pool (see InternShaderNameDX12).
struct InternedNameDX12 {
    const char *text="";
    bool empty() const { return !*text; }
    const char *c_str() const { return text; }
    bool operator==(const InternedNameDX12 &other) const { return text==other.text; }
    bool operator!=(const InternedNameDX12 &other) const { return text!=other.text; }
};
InternedNameDX12 InternShaderNameDX12(const char *name);
class CShaderDynamicDX12
{
public:
    void SetViewports( int nCount, const ShaderViewport_t* pViewports );
    int GetViewports( ShaderViewport_t* pViewports, int nMax ) const;
    double CurrentTime() const;
    void GetLightmapDimensions( int *w, int *h );
    MaterialFogMode_t GetSceneFogMode( );
    void GetSceneFogColor( unsigned char *rgb );
    void MatrixMode( MaterialMatrixMode_t matrixMode );
    void PushMatrix();
    void PopMatrix();
    void LoadMatrix( float *m );
    void MultMatrix( float *m );
    void MultMatrixLocal( float *m );
    void GetMatrix( MaterialMatrixMode_t matrixMode, float *dst );
    void LoadIdentity( void );
    void LoadCameraToWorld( void );
    void Ortho( double left, double right, double bottom, double top, double zNear, double zFar );
    void PerspectiveX( double fovx, double aspect, double zNear, double zFar );
    void PickMatrix( int x, int y, int width, int height );
    void Rotate( float angle, float x, float y, float z );
    void Translate( float x, float y, float z );
    void Scale( float x, float y, float z );
    void ScaleXY( float x, float y );
    void Color3f( float r, float g, float b );
    void Color3fv( float const* pColor );
    void Color4f( float r, float g, float b, float a );
    void Color4fv( float const* pColor );
    void Color3ub( unsigned char r, unsigned char g, unsigned char b );
    void Color3ubv( unsigned char const* pColor );
    void Color4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a );
    void Color4ubv( unsigned char const* pColor );
    void SetVertexShaderConstant( int var, float const* pVec, int numConst = 1, bool bForce = false );
    void SetPixelShaderConstant( int var, float const* pVec, int numConst = 1, bool bForce = false );
    void SetDefaultState();
    void GetWorldSpaceCameraPosition( float* pPos ) const;
    int GetCurrentNumBones( void ) const;
    int GetCurrentLightCombo( void ) const;
    MaterialFogMode_t GetCurrentFogType( void ) const;
    void SetTextureTransformDimension( TextureStage_t textureStage, int dimension, bool projected );
    void DisableTextureTransform( TextureStage_t textureStage );
    void SetBumpEnvMatrix( TextureStage_t textureStage, float m00, float m01, float m10, float m11 );
    void SetVertexShaderIndex( int vshIndex = -1 );
    void SetPixelShaderIndex( int pshIndex = 0 );
    void GetBackBufferDimensions( int& width, int& height ) const;
    int GetMaxLights( void ) const;
    const LightDesc_t& GetLight( int lightNum ) const;
    void SetPixelShaderFogParams( int reg );
    void SetVertexShaderStateAmbientLightCube();
    void SetPixelShaderStateAmbientLightCube( int pshReg, bool bForceToBlack = false );
    void CommitPixelShaderLighting( int pshReg );
    CMeshBuilder* GetVertexModifyBuilder();
    bool InFlashlightMode() const;
    const FlashlightState_t &GetFlashlightState( VMatrix &worldToTexture ) const;
    bool InEditorMode() const;
    MorphFormat_t GetBoundMorphFormat();
    void BindStandardTexture( Sampler_t sampler, StandardTextureId_t id );
    ITexture *GetRenderTargetEx( int nRenderTargetID );
    void SetToneMappingScaleLinear( const Vector &scale );
    const Vector &GetToneMappingScaleLinear( void ) const;
    float GetLightMapScaleFactor( void ) const;
    void LoadBoneMatrix( int boneIndex, const float *m );
    void PerspectiveOffCenterX( double fovx, double aspect, double zNear, double zFar, double bottom, double top, double left, double right );
    void SetFloatRenderingParameter(int parm_number, float value);
    void SetStencilEnable(bool onoff);
    void SetStencilFailOperation(StencilOperation_t op);
    void SetStencilZFailOperation(StencilOperation_t op);
    void SetStencilPassOperation(StencilOperation_t op);
    void SetStencilCompareFunction(StencilComparisonFunction_t cmpfn);
    void SetStencilReferenceValue(int ref);
    void SetStencilTestMask(uint32 msk);
    void SetStencilWriteMask(uint32 msk);
    void ClearStencilBufferRectangle( int xmin, int ymin, int xmax, int ymax,int value);
    void GetDXLevelDefaults(uint &max_dxlevel,uint &recommended_dxlevel);
    const FlashlightState_t &GetFlashlightStateEx( VMatrix &worldToTexture, ITexture **pFlashlightDepthTexture ) const;
    float GetAmbientLightCubeLuminance();
    void GetDX9LightState( LightState_t *state ) const;
    int GetPixelFogCombo( );
    void BindStandardVertexTexture( VertexTextureSampler_t sampler, StandardTextureId_t id );
    bool IsHWMorphingEnabled( ) const;
    void GetStandardTextureDimensions( int *pWidth, int *pHeight, StandardTextureId_t id );
    void SetBooleanVertexShaderConstant( int var, BOOL const* pVec, int numBools = 1, bool bForce = false );
    void SetIntegerVertexShaderConstant( int var, int const* pVec, int numIntVecs = 1, bool bForce = false );
    void SetBooleanPixelShaderConstant( int var, BOOL const* pVec, int numBools = 1, bool bForce = false );
    void SetIntegerPixelShaderConstant( int var, int const* pVec, int numIntVecs = 1, bool bForce = false );
    bool ShouldWriteDepthToDestAlpha( void ) const;
    void PushDeformation( DeformationBase_t const *Deformation );
    void PopDeformation( );
    int GetPackedDeformationInformation( int nMaskOfUnderstoodDeformations, float *pConstantValuesOut, int nBufferSize, int nMaximumDeformations, int *pNumDefsOut ) const;
    void MarkUnusedVertexFields( unsigned int nFlags, int nTexCoordCount, bool *pUnusedTexCoords );
    void GetCurrentColorCorrection( ShaderColorCorrectionInfo_t* pInfo );
    void SetPSNearAndFarZ( int pshReg );
    void SetDepthFeatheringPixelShaderConstant( int iConstant, float fDepthBlendScale );
    int GetPixelFogCombo1( bool bSupportsRadial );
    void SetDevice(CShaderDeviceDX12 *device) { device_ = device; }
protected:
    CShaderDeviceDX12 *device_ = nullptr;
    std::array<ShaderViewport_t,16> viewports_{};
    int viewportCount_ = 0;
    std::array<std::array<float,4>,256> vsFloat_{};
    std::array<std::array<float,4>,224> psFloat_{};
    std::array<std::array<int,4>,16> vsInt_{};
    std::array<std::array<int,4>,16> psInt_{};
    std::array<bool,16> vsBool_{};
    std::array<bool,16> psBool_{};
    std::array<uint64_t,6> constantVersions_{{1,1,1,1,1,1}};
    // Material space-1 blocks written through the private pointer bridge (VS b2-b7, PS b1-b7); sticky like registers.
    struct NativeCBufferSlotDX12 { std::vector<unsigned char> bytes; const dx12native::NativeCBufferLegacyMapDX12 *legacyMap=nullptr; uint64_t layoutHash=0, version=0; uint32_t byteSize=0; bool written=false; };
    std::array<NativeCBufferSlotDX12,6> nativeVSBlocks_{};
    std::array<NativeCBufferSlotDX12,7> nativePSBlocks_{};
    uint64_t nativeVSVersion_=1, nativePSVersion_=1;
    // Engine-owned space-1 blocks for native records, rebuilt only when their legacy inputs change.
    dx12native::DX12VSEngine nativeVSEngine_{};
    dx12native::DX12VSBones nativeVSBones_{};
    dx12native::DX12PSEngine nativePSEngine_{};
    std::array<uint64_t,5> nativeVSEngineInputs_{};
    uint64_t nativeVSEngineVersion_=1, nativePSEngineVersion_=1;
    std::array<uint64_t,4> extensionVersions_{};
    ShaderVertexExtensionDX12 previousVertexExtension_{};
    uint32_t vertexExtensionClipMask_=0;
    ShaderPixelExtensionDX12 previousPixelExtension_{};
    ShaderBumpExtensionDX12 previousBumpExtension_{};
    bool bumpExtensionDirty_=true;
    std::array<Vector,64> renderingVectors_{};
    uint64_t fixedVSVersion_=1, fixedPSVersion_=1;
    std::array<float,64> renderingFloats_{};
    std::array<int,64> renderingInts_{};
    std::array<ShaderAPITextureHandle_t,TEXTURE_MAX_STD_TEXTURES> standardTextures_{};
    ShaderAPITextureHandle_t fullScreenTexture_=INVALID_SHADERAPI_TEXTURE_HANDLE;
    ShaderAPITextureHandle_t gammaConversionTexture_=INVALID_SHADERAPI_TEXTURE_HANDLE;
    ShaderAPITextureHandle_t gammaIdentityTexture_=INVALID_SHADERAPI_TEXTURE_HANDLE;
    VMatrix matrices_[MATERIAL_MODEL_MAX + 1];
    MaterialMatrixMode_t matrixMode_ = MATERIAL_MODEL;
    Vector color_{1,1,1};
    float colorAlpha_ = 1.0f;
    Vector cameraPosition_{};
    Vector toneScale_{1,1,1};
    LightDesc_t *lights_ = nullptr;
    LightState_t lightState_{};
    FlashlightState_t flashlight_{};
    VMatrix flashlightMatrix_{};
    MaterialFogMode_t fogMode_ = MATERIAL_FOG_NONE;
    unsigned char fogColor_[3] = {0,0,0};
    int vertexShaderIndex_ = -1, pixelShaderIndex_ = 0;
    int boneCount_ = 0;
    // Number of model-matrix rows needed by the motion history block. This is
    // independent from the vertex weight-channel count.
    int motionBoneRows_ = 1;
    bool flashlightMode_ = false, editorMode_ = false, morphing_ = false;
    CUtlVector<BoxDeformation_t> deformations_;
    std::array<CUtlVector<VMatrix>, MATERIAL_MODEL_MAX + 1> matrixStacks_;
};

class CShaderAPIDX12 final : public CShaderDynamicDX12, public IShaderAPI, public IDebugTextureInfo
{
public:
    CShaderAPIDX12();
    ~CShaderAPIDX12();
    void ProcessPendingTextureDeletes();
    void SetViewports( int nCount, const ShaderViewport_t* pViewports ) override;
    int GetViewports( ShaderViewport_t* pViewports, int nMax ) const override;
    double CurrentTime() const override;
    void GetLightmapDimensions( int *w, int *h ) override;
    MaterialFogMode_t GetSceneFogMode( ) override;
    void GetSceneFogColor( unsigned char *rgb ) override;
    void MatrixMode( MaterialMatrixMode_t matrixMode ) override;
    void PushMatrix() override;
    void PopMatrix() override;
    void LoadMatrix( float *m ) override;
    void MultMatrix( float *m ) override;
    void MultMatrixLocal( float *m ) override;
    void GetMatrix( MaterialMatrixMode_t matrixMode, float *dst ) override;
    void LoadIdentity( void ) override;
    void LoadCameraToWorld( void ) override;
    void Ortho( double left, double right, double bottom, double top, double zNear, double zFar ) override;
    void PerspectiveX( double fovx, double aspect, double zNear, double zFar ) override;
    void PickMatrix( int x, int y, int width, int height ) override;
    void Rotate( float angle, float x, float y, float z ) override;
    void Translate( float x, float y, float z ) override;
    void Scale( float x, float y, float z ) override;
    void ScaleXY( float x, float y ) override;
    void Color3f( float r, float g, float b ) override;
    void Color3fv( float const* pColor ) override;
    void Color4f( float r, float g, float b, float a ) override;
    void Color4fv( float const* pColor ) override;
    void Color3ub( unsigned char r, unsigned char g, unsigned char b ) override;
    void Color3ubv( unsigned char const* pColor ) override;
    void Color4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a ) override;
    void Color4ubv( unsigned char const* pColor ) override;
    void SetVertexShaderConstant( int var, float const* pVec, int numConst = 1, bool bForce = false ) override;
    void SetPixelShaderConstant( int var, float const* pVec, int numConst = 1, bool bForce = false ) override;
    void SetDefaultState() override;
    void GetWorldSpaceCameraPosition( float* pPos ) const override;
    int GetCurrentNumBones( void ) const override;
    int GetCurrentLightCombo( void ) const override;
    MaterialFogMode_t GetCurrentFogType( void ) const override;
    void SetTextureTransformDimension( TextureStage_t textureStage, int dimension, bool projected ) override;
    void DisableTextureTransform( TextureStage_t textureStage ) override;
    void SetBumpEnvMatrix( TextureStage_t textureStage, float m00, float m01, float m10, float m11 ) override;
    void SetVertexShaderIndex( int vshIndex = -1 ) override;
    void SetPixelShaderIndex( int pshIndex = 0 ) override;
    void GetBackBufferDimensions( int& width, int& height ) const override;
    int GetMaxLights( void ) const override;
    const LightDesc_t& GetLight( int lightNum ) const override;
    void SetPixelShaderFogParams( int reg ) override;
    void SetVertexShaderStateAmbientLightCube() override;
    void SetPixelShaderStateAmbientLightCube( int pshReg, bool bForceToBlack = false ) override;
    void CommitPixelShaderLighting( int pshReg ) override;
    CMeshBuilder* GetVertexModifyBuilder() override;
    bool InFlashlightMode() const override;
    const FlashlightState_t &GetFlashlightState( VMatrix &worldToTexture ) const override;
    bool InEditorMode() const override;
    MorphFormat_t GetBoundMorphFormat() override;
    void BindStandardTexture( Sampler_t sampler, StandardTextureId_t id ) override;
    ITexture *GetRenderTargetEx( int nRenderTargetID ) override;
    void SetToneMappingScaleLinear( const Vector &scale ) override;
    const Vector &GetToneMappingScaleLinear( void ) const override;
    float GetLightMapScaleFactor( void ) const override;
    void LoadBoneMatrix( int boneIndex, const float *m ) override;
    void PerspectiveOffCenterX( double fovx, double aspect, double zNear, double zFar, double bottom, double top, double left, double right ) override;
    void SetFloatRenderingParameter(int parm_number, float value) override;
    void SetStencilEnable(bool onoff) override;
    void SetStencilFailOperation(StencilOperation_t op) override;
    void SetStencilZFailOperation(StencilOperation_t op) override;
    void SetStencilPassOperation(StencilOperation_t op) override;
    void SetStencilCompareFunction(StencilComparisonFunction_t cmpfn) override;
    void SetStencilReferenceValue(int ref) override;
    void SetStencilTestMask(uint32 msk) override;
    void SetStencilWriteMask(uint32 msk) override;
    void ClearStencilBufferRectangle( int xmin, int ymin, int xmax, int ymax,int value) override;
    void GetDXLevelDefaults(uint &max_dxlevel,uint &recommended_dxlevel) override;
    const FlashlightState_t &GetFlashlightStateEx( VMatrix &worldToTexture, ITexture **pFlashlightDepthTexture ) const override;
    float GetAmbientLightCubeLuminance() override;
    void GetDX9LightState( LightState_t *state ) const override;
    int GetPixelFogCombo( ) override;
    void BindStandardVertexTexture( VertexTextureSampler_t sampler, StandardTextureId_t id ) override;
    bool IsHWMorphingEnabled( ) const override;
    void GetStandardTextureDimensions( int *pWidth, int *pHeight, StandardTextureId_t id ) override;
    void SetBooleanVertexShaderConstant( int var, BOOL const* pVec, int numBools = 1, bool bForce = false ) override;
    void SetIntegerVertexShaderConstant( int var, int const* pVec, int numIntVecs = 1, bool bForce = false ) override;
    void SetBooleanPixelShaderConstant( int var, BOOL const* pVec, int numBools = 1, bool bForce = false ) override;
    void SetIntegerPixelShaderConstant( int var, int const* pVec, int numIntVecs = 1, bool bForce = false ) override;
    bool ShouldWriteDepthToDestAlpha( void ) const override;
    void PushDeformation( DeformationBase_t const *Deformation ) override;
    void PopDeformation( ) override;
    int GetNumActiveDeformations() const override;
    void ExecuteCommandBuffer( uint8 *pCmdBuffer ) override;
    void SetStandardTextureHandle( StandardTextureId_t nId, ShaderAPITextureHandle_t nHandle ) override;
    int GetPackedDeformationInformation( int nMaskOfUnderstoodDeformations, float *pConstantValuesOut, int nBufferSize, int nMaximumDeformations, int *pNumDefsOut ) const override;
    void MarkUnusedVertexFields( unsigned int nFlags, int nTexCoordCount, bool *pUnusedTexCoords ) override;
    void GetCurrentColorCorrection( ShaderColorCorrectionInfo_t* pInfo ) override;
    void SetPSNearAndFarZ( int pshReg ) override;
    void SetDepthFeatheringPixelShaderConstant( int iConstant, float fDepthBlendScale ) override;
    int GetPixelFogCombo1( bool bSupportsRadial ) override;
    void ClearBuffers( bool bClearColor, bool bClearDepth, bool bClearStencil, int renderTargetWidth, int renderTargetHeight ) override;
    void ClearColor3ub( unsigned char r, unsigned char g, unsigned char b ) override;
    void ClearColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a ) override;
    void BindVertexShader( VertexShaderHandle_t hVertexShader ) override;
    void BindGeometryShader( GeometryShaderHandle_t hGeometryShader ) override;
    void BindPixelShader( PixelShaderHandle_t hPixelShader ) override;
    void SetRasterState( const ShaderRasterState_t& state ) override;
    bool SetMode( void* hwnd, int nAdapter, const ShaderDeviceInfo_t &info ) override;
    void ChangeVideoMode( const ShaderDeviceInfo_t &info ) override;
    StateSnapshot_t TakeSnapshot( ) override;
    void TexMinFilter( ShaderTexFilterMode_t texFilterMode ) override;
    void TexMagFilter( ShaderTexFilterMode_t texFilterMode ) override;
    void TexWrap( ShaderTexCoordComponent_t coord, ShaderTexWrapMode_t wrapMode ) override;
    void CopyRenderTargetToTexture( ShaderAPITextureHandle_t textureHandle ) override;
    void Bind( IMaterial* pMaterial ) override;
    void FlushBufferedPrimitives() override;
    IMesh* GetDynamicMesh( IMaterial* pMaterial, int nHWSkinBoneCount, bool bBuffered = true, IMesh* pVertexOverride = 0, IMesh* pIndexOverride = 0) override;
    IMesh* GetDynamicMeshEx( IMaterial* pMaterial, VertexFormat_t vertexFormat, int nHWSkinBoneCount, bool bBuffered = true, IMesh* pVertexOverride = 0, IMesh* pIndexOverride = 0 ) override;
    bool IsTranslucent( StateSnapshot_t id ) const override;
    bool IsAlphaTested( StateSnapshot_t id ) const override;
    bool UsesVertexAndPixelShaders( StateSnapshot_t id ) const override;
    bool IsDepthWriteEnabled( StateSnapshot_t id ) const override;
    VertexFormat_t ComputeVertexFormat( int numSnapshots, StateSnapshot_t* pIds ) const override;
    VertexFormat_t ComputeVertexUsage( int numSnapshots, StateSnapshot_t* pIds ) const override;
    void BeginPass( StateSnapshot_t snapshot ) override;
    void RenderPass( int nPass, int nPassCount ) override;
    void SetNumBoneWeights( int numBones ) override;
    void SetLight( int lightNum, const LightDesc_t& desc ) override;
    void SetLightingOrigin( Vector vLightingOrigin ) override;
    void SetAmbientLight( float r, float g, float b ) override;
    void SetAmbientLightCube( Vector4D cube[6] ) override;
    void ShadeMode( ShaderShadeMode_t mode ) override;
    void CullMode( MaterialCullMode_t cullMode ) override;
    void ForceDepthFuncEquals( bool bEnable ) override;
    void OverrideDepthEnable( bool bEnable, bool bDepthEnable ) override;
    void SetHeightClipZ( float z ) override;
    void SetHeightClipMode( enum MaterialHeightClipMode_t heightClipMode ) override;
    void SetClipPlane( int index, const float *pPlane ) override;
    void EnableClipPlane( int index, bool bEnable ) override;
    void SetSkinningMatrices() override;
    ImageFormat GetNearestSupportedFormat( ImageFormat fmt, bool bFilteringRequired = true ) const override;
    ImageFormat GetNearestRenderTargetFormat( ImageFormat fmt ) const override;
    bool DoRenderTargetsNeedSeparateDepthBuffer() const override;
    ShaderAPITextureHandle_t CreateTexture( int width, int height, int depth, ImageFormat dstImageFormat, int numMipLevels, int numCopies, int flags, const char *pDebugName, const char *pTextureGroupName ) override;
    void DeleteTexture( ShaderAPITextureHandle_t textureHandle ) override;
    ShaderAPITextureHandle_t CreateDepthTexture( ImageFormat renderTargetFormat, int width, int height, const char *pDebugName, bool bTexture ) override;
    bool IsTexture( ShaderAPITextureHandle_t textureHandle ) override;
    bool IsTextureResident( ShaderAPITextureHandle_t textureHandle ) override;
    void ModifyTexture( ShaderAPITextureHandle_t textureHandle ) override;
    void TexImage2D( int level, int cubeFaceID, ImageFormat dstFormat, int zOffset, int width, int height, ImageFormat srcFormat, bool bSrcIsTiled, void *imageData ) override;
    void TexSubImage2D( int level, int cubeFaceID, int xOffset, int yOffset, int zOffset, int width, int height, ImageFormat srcFormat, int srcStride, bool bSrcIsTiled, void *imageData ) override;
    void TexImageFromVTF( IVTFTexture* pVTF, int iVTFFrame ) override;
    bool TexLock( int level, int cubeFaceID, int xOffset, int yOffset, int width, int height, CPixelWriter& writer ) override;
    void TexUnlock( ) override;
    void TexSetPriority( int priority ) override;
    void BindTexture( Sampler_t sampler, ShaderAPITextureHandle_t textureHandle ) override;
    void SetRenderTarget( ShaderAPITextureHandle_t colorTextureHandle = SHADER_RENDERTARGET_BACKBUFFER, ShaderAPITextureHandle_t depthTextureHandle = SHADER_RENDERTARGET_DEPTHBUFFER ) override;
    void ClearBuffersObeyStencil( bool bClearColor, bool bClearDepth ) override;
    void ReadPixels( int x, int y, int width, int height, unsigned char *data, ImageFormat dstFormat ) override;
    void ReadPixels( Rect_t *pSrcRect, Rect_t *pDstRect, unsigned char *data, ImageFormat dstFormat, int nDstStride ) override;
    void FlushHardware() override;
    void BeginFrame() override;
    void EndFrame() override;
    int SelectionMode( bool selectionMode ) override;
    void SelectionBuffer( unsigned int* pBuffer, int size ) override;
    void ClearSelectionNames( ) override;
    void LoadSelectionName( int name ) override;
    void PushSelectionName( int name ) override;
    void PopSelectionName() override;
    void ForceHardwareSync() override;
    void ClearSnapshots() override;
    void FogStart( float fStart ) override;
    void FogEnd( float fEnd ) override;
    void SetFogZ( float fogZ ) override;
    void SceneFogColor3ub( unsigned char r, unsigned char g, unsigned char b ) override;
    void SceneFogMode( MaterialFogMode_t fogMode ) override;
    bool CanDownloadTextures() const override;
    void ResetRenderState( bool bFullReset = true ) override;
    int GetCurrentDynamicVBSize( void ) override;
    void DestroyVertexBuffers( bool bExitingLevel = false ) override;
    void EvictManagedResources() override;
    void SetAnisotropicLevel( int nAnisotropyLevel ) override;
    void SyncToken( const char *pToken ) override;
    void SetStandardVertexShaderConstants( float fOverbright ) override;
    ShaderAPIOcclusionQuery_t CreateOcclusionQueryObject( void ) override;
    void DestroyOcclusionQueryObject( ShaderAPIOcclusionQuery_t ) override;
    void BeginOcclusionQueryDrawing( ShaderAPIOcclusionQuery_t ) override;
    void EndOcclusionQueryDrawing( ShaderAPIOcclusionQuery_t ) override;
    int OcclusionQuery_GetNumPixelsRendered( ShaderAPIOcclusionQuery_t hQuery, bool bFlush = false ) override;
    void SetFlashlightState( const FlashlightState_t &state, const VMatrix &worldToTexture ) override;
    void ClearVertexAndPixelShaderRefCounts() override;
    void PurgeUnusedVertexAndPixelShaders() override;
    void DXSupportLevelChanged() override;
    void EnableUserClipTransformOverride( bool bEnable ) override;
    void UserClipTransform( const VMatrix &worldToView ) override;
    MorphFormat_t ComputeMorphFormat( int numSnapshots, StateSnapshot_t* pIds ) const override;
    void SetRenderTargetEx( int nRenderTargetID, ShaderAPITextureHandle_t colorTextureHandle = SHADER_RENDERTARGET_BACKBUFFER, ShaderAPITextureHandle_t depthTextureHandle = SHADER_RENDERTARGET_DEPTHBUFFER ) override;
    void CopyRenderTargetToTextureEx( ShaderAPITextureHandle_t textureHandle, int nRenderTargetID, Rect_t *pSrcRect = NULL, Rect_t *pDstRect = NULL ) override;
    void CopyTextureToRenderTargetEx( int nRenderTargetID, ShaderAPITextureHandle_t textureHandle, Rect_t *pSrcRect = NULL, Rect_t *pDstRect = NULL ) override;
    void HandleDeviceLost() override;
    void EnableLinearColorSpaceFrameBuffer( bool bEnable ) override;
    void SetFullScreenTextureHandle( ShaderAPITextureHandle_t h ) override;
    void SetIntRenderingParameter(int parm_number, int value) override;
    void SetVectorRenderingParameter(int parm_number, Vector const &value) override;
    float GetFloatRenderingParameter(int parm_number) const override;
    int GetIntRenderingParameter(int parm_number) const override;
    Vector GetVectorRenderingParameter(int parm_number) const override;
    void SetFastClipPlane( const float *pPlane ) override;
    void EnableFastClip( bool bEnable ) override;
    void GetMaxToRender( IMesh *pMesh, bool bMaxUntilFlush, int *pMaxVerts, int *pMaxIndices ) override;
    int GetMaxVerticesToRender( IMaterial *pMaterial ) override;
    int GetMaxIndicesToRender( ) override;
    void DisableAllLocalLights() override;
    int CompareSnapshots( StateSnapshot_t snapshot0, StateSnapshot_t snapshot1 ) override;
    IMesh *GetFlexMesh() override;
    void SetFlashlightStateEx( const FlashlightState_t &state, const VMatrix &worldToTexture, ITexture *pFlashlightDepthTexture ) override;
    bool SupportsMSAAMode( int nMSAAMode ) override;
    bool OwnGPUResources( bool bEnable ) override;
    void GetFogDistances( float *fStart, float *fEnd, float *fFogZ ) override;
    void BeginPIXEvent( unsigned long color, const char *szName ) override;
    void EndPIXEvent() override;
    void SetPIXMarker( unsigned long color, const char *szName ) override;
    void EnableAlphaToCoverage() override;
    void DisableAlphaToCoverage() override;
    void ComputeVertexDescription( unsigned char* pBuffer, VertexFormat_t vertexFormat, MeshDesc_t& desc ) const override;
    bool SupportsShadowDepthTextures( void ) override;
    void SetDisallowAccess( bool ) override;
    void EnableShaderShaderMutex( bool ) override;
    void ShaderLock() override;
    void ShaderUnlock() override;
    ImageFormat GetShadowDepthTextureFormat( void ) override;
    bool SupportsFetch4( void ) override;
    void SetShadowDepthBiasFactors( float fShadowSlopeScaleDepthBias, float fShadowDepthBias ) override;
    void BindVertexBuffer( int nStreamID, IVertexBuffer *pVertexBuffer, int nOffsetInBytes, int nFirstVertex, int nVertexCount, VertexFormat_t fmt, int nRepetitions = 1 ) override;
    void BindIndexBuffer( IIndexBuffer *pIndexBuffer, int nOffsetInBytes ) override;
    void Draw( MaterialPrimitiveType_t primitiveType, int nFirstIndex, int nIndexCount ) override;
    void PerformFullScreenStencilOperation( void ) override;
    void SetScissorRect( const int nLeft, const int nTop, const int nRight, const int nBottom, const bool bEnableScissor ) override;
    bool SupportsCSAAMode( int nNumSamples, int nQualityLevel ) override;
    void InvalidateDelayedShaderConstants( void ) override;
    void SetLinearToGammaConversionTextures( ShaderAPITextureHandle_t hSRGBWriteEnabledTexture, ShaderAPITextureHandle_t hIdentityTexture ) override;
    ImageFormat GetNullTextureFormat( void ) override;
    void BindVertexTexture( VertexTextureSampler_t nSampler, ShaderAPITextureHandle_t textureHandle ) override;
    void EnableHWMorphing( bool bEnable ) override;
    void SetFlexWeights( int nFirstWeight, int nCount, const MorphWeight_t* pWeights ) override;
    void FogMaxDensity( float flMaxDensity ) override;
    void CreateTextures( ShaderAPITextureHandle_t *pHandles, int count, int width, int height, int depth, ImageFormat dstImageFormat, int numMipLevels, int numCopies, int flags, const char *pDebugName, const char *pTextureGroupName ) override;
    void AcquireThreadOwnership() override;
    void ReleaseThreadOwnership() override;
    void EnableBuffer2FramesAhead( bool bEnable ) override;
    float GammaToLinear_HardwareSpecific( float fGamma ) const override;
    float LinearToGamma_HardwareSpecific( float fLinear ) const override;
    void PrintfVA( char *fmt, va_list vargs ) override;
    void Printf( PRINTF_FORMAT_STRING const char *fmt, ... ) override;
    float Knob( char *knobname, float *setvalue = NULL ) override;
    void OverrideAlphaWriteEnable( bool bEnable, bool bAlphaWriteEnable ) override;
    void OverrideColorWriteEnable( bool bOverrideEnable, bool bColorWriteEnable ) override;
    void ClearBuffersObeyStencilEx( bool bClearColor, bool bClearAlpha, bool bClearDepth ) override;
    void CopyRenderTargetToScratchTexture( ShaderAPITextureHandle_t srcRt, ShaderAPITextureHandle_t dstTex, Rect_t *pSrcRect = NULL, Rect_t *pDstRect = NULL ) override;
    void LockRect( void** pOutBits, int* pOutPitch, ShaderAPITextureHandle_t texHandle, int mipmap, int x, int y, int w, int h, bool bWrite, bool bRead ) override;
    void UnlockRect( ShaderAPITextureHandle_t texHandle, int mipmap ) override;
    void TexLodClamp( int finest ) override;
    void TexLodBias( float bias ) override;
    void CopyTextureToTexture( ShaderAPITextureHandle_t srcTex, ShaderAPITextureHandle_t dstTex ) override;
    int VertexFormatSize( VertexFormat_t vertexFormat ) const override;
    void SceneFogRadial( bool bRadial ) override;
    bool GetSceneFogRadial() override;
    void SetDevice(CShaderDeviceDX12 *device) { CShaderDynamicDX12::SetDevice(device); }
    void SetShaderUtil(IShaderUtil *util) { shaderUtil_ = util; }
    bool InitializeDeviceResources(CShaderDeviceDX12 *device);
    void ShutdownDeviceResources();
    // Development-only shader_precache: any thread queues; the recording owner validates in BeginFrame.
    void QueueShaderPrecacheRequest(const char *name, int staticIndex, int dynamicIndex);
    void ProcessShaderPrecacheRequests();
    void SetShaderPrecacheAccepting(bool accepting);
    void ApplyNativeCBufferWrite(const dx12native::NativeCBufferWriteDX12 &write,bool pixel);
    struct PrecacheRequestDX12 { CUtlString name; int staticIndex = -1, dynamicIndex = -1; };
    CThreadFastMutex precacheMutex_;
    CUtlVector<PrecacheRequestDX12> precacheRequests_;
    bool precacheAccepting_ = false;
    // IDebugTextureInfo
    void EnableDebugTextureList(bool bEnable) override;
    void EnableGetAllTextures(bool bEnable) override;
    KeyValues *GetDebugTextureList() override;
    int GetTextureMemoryUsed(TextureMemoryType eTextureMemory) override;
    bool IsDebugTextureListFresh(int numFramesAllowed = 1) override;
    bool SetDebugTextureRendering(bool bEnable) override;
    void DrawMesh(CMeshDX12 *mesh, int firstIndex, int indexCount);
    void DrawMaterialMesh(CMeshDX12 *mesh, int firstIndex, int indexCount);
    void RetireShaderPipelines(ShaderRecordDX12 *record);
    void ResetNativeState();
    ShaderRecordDX12 *ResolveNamedShader(const char *name,bool pixel,int staticIndex,int dynamicIndex);
    bool PrepareSampledTexture( ShaderAPITextureHandle_t textureHandle, bool srgb, ID3D12Resource **resource, D3D12_SHADER_RESOURCE_VIEW_DESC &srv, D3D12_SAMPLER_DESC &sampler, D3D12_CPU_DESCRIPTOR_HANDLE *source = nullptr, bool comparison = false );
    bool PrepareRenderTargets( RenderTargetBindingDX12 &binding, bool encodeSRGB = true );
    void ReleaseTextureDeviceResources();
    bool PresentGamma(ID3D12Resource *back, float gamma, float tvMin, float tvMax, float tvExponent, bool tvEnabled);
    int StencilReference() const { return stencilRef_; }
    uint8_t StencilReadMask() const { return stencilReadMask_; }
    StencilComparisonFunction_t StencilCompare() const { return stencilCompare_; }
public:
    friend bool PrepareSampledTextureDX12( CShaderAPIDX12 &, ShaderAPITextureHandle_t, bool, ID3D12Resource **, D3D12_SHADER_RESOURCE_VIEW_DESC &, D3D12_SAMPLER_DESC &, D3D12_CPU_DESCRIPTOR_HANDLE *, bool );
    friend bool PrepareRenderTargetsDX12( CShaderAPIDX12 &, RenderTargetBindingDX12 &, bool );
    struct TextureRecord {
        ShaderAPITextureHandle_t id = 0;
        int width = 0, height = 0, depth = 1, mipLevels = 1, copies = 1;
        int currentCopy = 0;
        bool switchNeeded = false;
        size_t bytesPerCopy = 0;
        ImageFormat format = IMAGE_FORMAT_UNKNOWN, requestedFormat = IMAGE_FORMAT_UNKNOWN;
        int flags = 0, priority = 0, binds = 0;
        std::string name;
        CUtlVector<unsigned char> pixels;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        struct ResourceCopy { Microsoft::WRL::ComPtr<ID3D12Resource> resource; };
        CUtlVector<ResourceCopy> resources;
        D3D12_CPU_DESCRIPTOR_HANDLE srvSources_[2]{};
        ID3D12Resource *srvResources_[2]{};
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDescriptors_[2]{};
        D3D12_SAMPLER_DESC samplerDescriptors_[2]{}; bool samplerDescriptorValid_[2]{}; int samplerDescriptorAnisotropy_=-1;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{}, rtvSRGB{};
        CUtlVector<unsigned char> dirtySubresources;
        CUtlVector<unsigned char> initializedSubresources;
        CUtlVector<unsigned char> gpuAuthoritativeSubresources;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        CUtlVector<D3D12_RESOURCE_STATES> subresourceStates;
        CUtlVector<unsigned char> lockData;
        int lockLevel = -1, lockFace = 0, lockX = 0, lockY = 0, lockWidth = 0, lockHeight = 0, lockPitch = 0;
        bool lockWrite = false, lockRead = false, resourceResident = false, sampledStateValid = false;
        int minFilter=1,magFilter=1;int wrapU=1,wrapV=1,wrapW=1;float lodBias=0;int lodClamp=0;bool gpuDirty=true;
    };
    struct PreparedTextureSlot { ShaderAPITextureHandle_t handle=0; TextureRecord *record=nullptr; ID3D12Resource *resource=nullptr; D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; D3D12_SAMPLER_DESC sampler{}; D3D12_CPU_DESCRIPTOR_HANDLE source{}; uint64_t epoch=0; uint16_t samplerId=0; bool srgb=false,comparison=false,valid=false; };
    std::array<PreparedTextureSlot,32> preparedTextureSlots_{};
    struct TextureSetKeyDX12 { std::array<ShaderAPITextureHandle_t,16> pixel; std::array<ShaderAPITextureHandle_t,4> vertex; uint64_t epoch,fence,nullView; uint32_t sampledMask,srgbMask,comparisonMask; };
    TextureSetKeyDX12 lastTextureSet_{};
    bool textureSetValid_=false;
    // Bumped by every texture change that can affect a prepared sampled slot.
    uint64_t textureStateEpoch_=1;
    // Bumped when a texture's resource, copy or views change identity (create, rotate, release, delete).
    uint64_t textureIdentityEpoch_=1;
    struct RenderTargetCacheDX12 {
        bool valid=false,srgb=false; uint64_t identityEpoch=0; ShaderAPITextureHandle_t depthTarget=0;
        std::array<ShaderAPITextureHandle_t,RenderTargetBindingDX12::kMaxColorTargets> handles{};
        ID3D12Resource *sceneColor=nullptr,*sceneDepth=nullptr; SIZE_T sceneRtv=0;
        RenderTargetBindingDX12 binding{}; std::array<TextureRecord *,RenderTargetBindingDX12::kMaxColorTargets> records{}; TextureRecord *depth=nullptr;
    } renderTargetCache_;
    DescriptorRangeDX12 preparedSamplerTable_{};
    uint64_t preparedSamplerFence_=0;
    uint32_t preparedSamplerMask_=0;
    struct InputLayoutCache { ShaderRecordDX12 *shader=nullptr; uint64_t variant=0,policy=0,layoutKey=0; VertexFormat_t format=0; uint32_t instanceCount=0; uint8_t streamFlags=0; UINT count=0; bool zeroInput=false,valid=false; std::array<D3D12_INPUT_ELEMENT_DESC,MAX_VERTEX_INPUTS_DX12> elements{}; };
    // Direct-mapped by shader/variant/format/policy; entries whose shader is retired are invalidated.
    std::array<InputLayoutCache,64> inputLayoutCaches_{};
    // Draw-to-draw reused binding input; slots outside drawBindingMask_ always hold the null view.
    CPipelineCacheDX12::BindingInputDX12 drawBindingInput_{D3D12_CPU_DESCRIPTOR_HANDLE{}};
    D3D12_CPU_DESCRIPTOR_HANDLE drawBindingNull_{};
    uint32_t drawBindingMask_=0;
    // Bound-texture dimension per slot; handle values are never reused and DeleteTexture clears matches.
    std::array<ShaderAPITextureHandle_t,16> textureTypeHandles_{};
    std::array<uint8_t,16> textureTypeValues_{};
    // Every input of the pipeline-selection part of DrawBuffers (translation, input layout, PSO).
    // Zero-filled before assignment so memcmp is exact.
    struct PipelineSignatureDX12 {
        uint64_t resolveEpoch,vs,ps,gs,format,layoutKey,policy;
        uint32_t motionPass;
        int64_t snapshot;
        uint64_t textureTypes;
        std::array<DXGI_FORMAT,RenderTargetBindingDX12::kMaxColorTargets> colorFormats;
        DXGI_FORMAT depthFormat;
        uint32_t instanceCount,primitive,streamFlags,clipMask,colorCount,samples,quality,hasDepth;
        ShaderRasterState_t rasterState;
        uint32_t rasterOverride,shadeMode,fogMode,pixelFog,cullMode;
        uint32_t stencilEnabled,stencilCompare,stencilFail,stencilDepthFail,stencilPass,stencilReadMask,stencilWriteMask;
        uint32_t alphaToCoverage,colorWriteOverride,colorWriteValue,alphaWriteOverride,alphaWriteValue,overrideDepthEnable,overrideDepthValue,forceDepthEquals,reverseDepth;
        float shadowSlope,shadowDepth,slopeDecal,slopeNormal,depthDecal,depthNormal;
    };
    struct PipelineMemoDX12 {
        PipelineSignatureDX12 signature{};
        ShaderRecordDX12 *vs=nullptr,*ps=nullptr; ID3D12PipelineState *pso=nullptr;
        uint64_t vsVariant=0,psVariant=0,epoch=0,psoEpoch=0;
        bool depthOnly=false,generatedVS=false,generatedPS=false,zeroInput=false,geometryStage=false;
    };
    struct DrawStatsDX12 { uint64_t draws=0,memoHits=0; } drawStats_;
    uint32_t drawStatsFrames_=0;
    // Direct-mapped pipeline memo. Entries are valid for the current epoch, which is bumped whenever
    // shader records, snapshots or the device go away; PSO lifetime is tracked by the pipeline epoch.
    std::array<PipelineMemoDX12,256> pipelineMemos_{};
    uint64_t pipelineMemoEpoch_=1;
    // Last translation state key and the exact inputs that produced it.
    ShaderRasterStateDX12 translationKeyMemoRaster_{};
    uint64_t translationKeyMemoLayout_=0,translationKeyMemo_=0;
    bool translationKeyMemoValid_=false;
private:
    struct VertexBindingDX12 {
        CVertexBufferDX12 *buffer=nullptr;
        uint32_t byteOffset=0, firstVertex=0, vertexCount=0, repetitions=1;
        VertexFormat_t format=0;
    };
    void DrawBuffers(const std::array<VertexBindingDX12,16> &streams, CIndexBufferDX12 *indices,
                     size_t indexOffset, MaterialPrimitiveType_t primitive, int firstIndex, int indexCount,
                     bool meshStreams=false);
    void SetMotionPass(int mode);
    bool EnsureMotionResources();
    bool PrepareMotionBinding(RenderTargetBindingDX12 &binding);
    void DrawMotionReprojection();
    void ResolveMotionTarget();
    ShaderRecordDX12 *MotionVertexShader(VertexFormat_t format);
    void FillMotionBlock(const VertexBindingDX12 &vb, CIndexBufferDX12 *ib, size_t indexOffset, int firstIndex, int indexCount);
    void MarkMotionTargetStale();
    bool MotionPassActive() const { return motionPassState_ == MotionPassStateDX12::Active; }
    void ReleaseMotionResources();
    void TransitionMotionTarget(D3D12_RESOURCE_STATES desired);
    CMeshBuilder vertexModifyBuilder_;
    struct NamedShaderKeyView {
        const char *name;
        int staticIndex, dynamicIndex;
        bool pixel;
    };
    struct NamedShaderKey {
        CUtlString name;
        int staticIndex, dynamicIndex;
        bool pixel;
        NamedShaderKey(NamedShaderKeyView key) : name(key.name), staticIndex(key.staticIndex), dynamicIndex(key.dynamicIndex), pixel(key.pixel) {}
        operator NamedShaderKeyView() const { return {name.String(),staticIndex,dynamicIndex,pixel}; }
    };
    struct NamedShaderHash {
        unsigned operator()(NamedShaderKeyView key) const {
            unsigned hash=StringHashFunctor()(key.name);
            hash=Mix32HashFunctor()(hash^static_cast<uint32>(key.staticIndex));
            hash=Mix32HashFunctor()(hash^static_cast<uint32>(key.dynamicIndex));
            return Mix32HashFunctor()(hash^static_cast<uint32>(key.pixel));
        }
    };
    struct NamedShaderEqual {
        bool operator()(NamedShaderKeyView a,NamedShaderKeyView b) const {
            return a.staticIndex==b.staticIndex && a.dynamicIndex==b.dynamicIndex && a.pixel==b.pixel && StringEqualFunctor()(a.name,b.name);
        }
    };
    CUtlHashtable<NamedShaderKey,bool,NamedShaderHash,NamedShaderEqual,NamedShaderKeyView> namedShaderReferences_;
    CUtlHashtable<CUtlString,float,StringHashFunctor,StringEqualFunctor,const char *> knobs_;
    unsigned int unusedVertexFields_=0;
    std::array<bool,VERTEX_MAX_TEXTURE_COORDINATES> unusedTextureCoordinates_{};
    void MatrixChanged();
    void CommitTransforms();
    bool ShouldUsePixelFog() const;
    void CommitFogState();
    bool transformsDirty_ = true;
    struct SourceLayoutEntryDX12 { VertexFormat_t format=0; uint8_t flags=0; bool valid=false; uint64_t translationKey=0; VertexLayoutDX12 layout{}; };
    std::array<SourceLayoutEntryDX12,16> sourceLayouts_{};
    uint64_t sourceLayoutTranslationKey_=0;
    int maxBoneLoaded_ = 0;
    int SortLocalLights(std::array<int,4> &indices) const;
    void CommitVertexLighting();
    std::array<LightDesc_t,4> lights_{};
    std::array<std::array<float,4>,6> ambientCube_{};
    Vector lightingOrigin_{0.f,0.f,0.f};
    bool lightingDirty_ = false;
    bool namedVertexShaderDirty_ = false, namedPixelShaderDirty_ = false;
    bool boundVertexShaderIsNamed_=false,boundPixelShaderIsNamed_=false;
    bool EnsureTextureResident(TextureRecord &texture);
    bool AllocateNativeTexture(TextureRecord &texture);
    bool RefreshTextureStaging(TextureRecord &texture, int face, int mip);
    void AdvanceTextureCopy(TextureRecord &texture);
    struct Snapshot {
        bool translucent=false, alphaTest=false, depthWrite=true, depthTest=true, shaders=false, colorWrites=true, alphaWrites=true, culling=true, stencil=false, alphaToCoverage=false, separateAlpha=false, srgbWrite=false, fogGammaDisabled=false;
        ShaderFogMode_t fogMode=SHADER_FOGMODE_DISABLED;
        float alphaReference=0.f;
        ShaderAlphaFunc_t alphaFunction=SHADER_ALPHAFUNC_ALWAYS;
        ShaderBlendFactor_t blendSource=SHADER_BLEND_ONE,blendDestination=SHADER_BLEND_ZERO,blendAlphaSource=SHADER_BLEND_ONE,blendAlphaDestination=SHADER_BLEND_ZERO;
        ShaderBlendOp_t blendOperation=SHADER_BLEND_OP_ADD,blendAlphaOperation=SHADER_BLEND_OP_ADD;
        ShaderStencilFunc_t stencilFunction=SHADER_STENCILFUNC_ALWAYS;
        ShaderStencilOp_t stencilFail=SHADER_STENCILOP_KEEP,stencilDepthFail=SHADER_STENCILOP_KEEP,stencilPass=SHADER_STENCILOP_KEEP;
        uint8_t stencilReadMask=0xff,stencilWriteMask=0xff,stencilReference=0;
        ShaderDepthFunc_t depthFunction=SHADER_DEPTHFUNC_NEAREROREQUAL;
        VertexFormat_t format=0, usage=0;
        InternedNameDX12 vertexShaderName, pixelShaderName;
        int staticVertexIndex=0,staticPixelIndex=0;
        MorphFormat_t morph=0;
        uint16_t comparisonSamplerMask=0, srgbReadMask=0;
        FixedFunctionStateDX12 fixed{};
        PolygonOffsetMode_t polygonOffset=SHADER_POLYOFFSET_DISABLE;
        ShaderPolyMode_t polyFront=SHADER_POLYMODE_FILL,polyBack=SHADER_POLYMODE_FILL;
    };
    CThreadFastMutex stateMutex_;
    CPipelineCacheDX12 pipeline_;
    struct ClearPassDX12 {
        DXGI_FORMAT color = DXGI_FORMAT_UNKNOWN, depth = DXGI_FORMAT_UNKNOWN;
        std::array<DXGI_FORMAT,RenderTargetBindingDX12::kMaxColorTargets> colors{};
        int colorCount = 0;
        UINT samples = 1;
        UINT quality = 0;
        UINT8 mask = 0;
        D3D12_COMPARISON_FUNC stencilCompare = D3D12_COMPARISON_FUNC_ALWAYS;
        UINT8 stencilReadMask = 0xff;
        UINT8 stencilWriteMask = 0;
        D3D12_STENCIL_OP stencilPass = D3D12_STENCIL_OP_KEEP, stencilFail = D3D12_STENCIL_OP_KEEP;
        bool stencilOnly = false;
        bool depthWrite = false;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    };
    Microsoft::WRL::ComPtr<ID3D12RootSignature> clearRoot_;
    CUtlVector<ClearPassDX12> clearPasses_;
    void DrawMaskedClear(bool rgb, bool alpha, bool depth, const D3D12_RECT *rect = nullptr, bool stencilOnly = false);
    struct BlitPassDX12 {
        DXGI_FORMAT color = DXGI_FORMAT_UNKNOWN;
        UINT samples = 1;
        UINT quality = 0;
        bool gamma = false;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    };
    struct ResolveTextureRecord {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_DESC desc{};
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RESOLVE_DEST;
        uint64_t lastUseFence = 0;
    };
    ResolveTextureRecord *AcquireResolveTexture(const D3D12_RESOURCE_DESC &desc, HRESULT &creationResult);
    CUtlVector<ResolveTextureRecord *> resolveTextures_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> blitRoot_;
    CUtlVector<BlitPassDX12> blitPasses_;
    bool BlitTexture(ID3D12Resource *source, D3D12_RESOURCE_STATES &sourceState, bool sourceIsScene, ImageFormat sourceFormat, ID3D12Resource *destination, D3D12_RESOURCE_STATES &destinationState, bool destinationIsScene, ImageFormat destinationFormat, Rect_t sourceRect, Rect_t destinationRect, bool sourceSRGB, bool destinationSRGB, const float *gammaCoefficients = nullptr);
    void CopyTextureRegionDX12(ShaderAPITextureHandle_t source, ShaderAPITextureHandle_t destination, Rect_t *sourceRect, Rect_t *destinationRect);
    struct OcclusionQueryDX12;
    CUtlVector<OcclusionQueryDX12 *> occlusionQueries_;
    CUtlHashtable<ShaderAPITextureHandle_t, TextureRecord *> textures_;
    // Handles are never reused, so a direct-mapped cache of live records only needs clearing on deletion.
    struct TextureLookupDX12 { ShaderAPITextureHandle_t handle=0; TextureRecord *record=nullptr; };
    mutable std::array<TextureLookupDX12,256> textureLookup_{};
    TextureRecord *FindTexture(ShaderAPITextureHandle_t handle) const {
        auto &cached = textureLookup_[static_cast<size_t>(handle) & (textureLookup_.size()-1)];
        if (cached.record && cached.handle == handle) return cached.record;
        const auto entry = textures_.Find(handle);
        if (entry == textures_.InvalidHandle()) return nullptr;
        cached = { handle, textures_[entry] };
        return cached.record;
    }
    struct RetiredTextureViewsDX12 {
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv, dsv;
        uint64_t fence = 0;
    };
    CUtlVector<RetiredTextureViewsDX12> retiredTextureViews_;
    ShaderAPITextureHandle_t nextTexture_ = 16;
    ShaderAPITextureHandle_t modifiedTexture_ = 0;
    std::array<ShaderAPITextureHandle_t,16> boundTextures_{};
    std::array<ShaderAPITextureHandle_t,4> vertexTextures_{};
    // Snapshot strings must not be byte-relocated when this append-only cache grows.
    CUtlBlockVector<Snapshot> snapshots_;
    Snapshot shadowState_{};
    ShaderRasterState_t rasterState_{};
    bool rasterOverride_=false;
    CUtlHashtable<CUtlString,ShaderVcsFile *,StringHashFunctor,StringEqualFunctor,const char *> namedShaderFiles_;
    CUtlHashtable<NamedShaderKey,ShaderRecordDX12 *,NamedShaderHash,NamedShaderEqual,NamedShaderKeyView> namedShaderCombos_;
    struct FixedShaderKey {
        StateSnapshot_t snapshot;
        VertexFormat_t format;
        bool pixel;
        uint32_t textureTypes;
        uint64_t linkage;
        bool operator<(const FixedShaderKey &other) const {
            if(snapshot!=other.snapshot)return snapshot<other.snapshot;
            if(format!=other.format)return format<other.format;
            if(pixel!=other.pixel)return pixel<other.pixel;
            if(textureTypes!=other.textureTypes)return textureTypes<other.textureTypes;
            return linkage<other.linkage;
        }
    };
    CUtlMap<FixedShaderKey,ShaderRecordDX12 *,uint32_t> fixedShaders_{DefLessFunc(FixedShaderKey)};
    std::array<bool,8> textureTransformEnabled_{};
    std::array<int,8> textureTransformDimension_{};
    std::array<bool,8> textureTransformProjected_{};
    std::array<std::array<float,4>,8> bumpMatrices_{};
    // Bit per texture matrix known to be identity; any other matrix write clears it (see MatrixChanged).
    uint32_t textureMatrixIdentityMask_=0;
    Vector ambientLight_{0.f,0.f,0.f};
    ShaderShadeMode_t shadeMode_=SHADER_SMOOTH;
    bool forceDepthEquals_=false,overrideDepthEnable_=false,overrideDepthValue_=true;
    MaterialCullMode_t cullMode_=MATERIAL_CULLMODE_CCW;
    Snapshot activeSnapshot_{};
    StateSnapshot_t activeSnapshotId_=static_cast<StateSnapshot_t>(-1);
    bool linearColorSpaceFramebuffer_=false;
    bool EffectiveSRGBWrite() const { return activeSnapshot_.srgbWrite && !linearColorSpaceFramebuffer_; }
    ShaderAPITextureHandle_t renderTarget_ = SHADER_RENDERTARGET_BACKBUFFER;
    std::array<ShaderAPITextureHandle_t,RenderTargetBindingDX12::kMaxColorTargets> renderTargets_{{SHADER_RENDERTARGET_BACKBUFFER,SHADER_RENDERTARGET_NONE,SHADER_RENDERTARGET_NONE,SHADER_RENDERTARGET_NONE}};
    ShaderAPITextureHandle_t depthTarget_ = SHADER_RENDERTARGET_DEPTHBUFFER;
    float clearColor_[4] = {0,0,0,1};
    int anisotropy_ = 1;
    bool debugList_ = false, debugAll_ = false, debugRender_ = false;
    uint64_t debugListFrame_ = 0;
    KeyValues *debugTextureEntries_ = nullptr;
    IShaderUtil *shaderUtil_ = nullptr;
    CSelectionStateDX12 selection_;
    int stencilRef_=0;
    uint8_t stencilReadMask_=0xff,stencilWriteMask_=0xff;
    StencilComparisonFunction_t stencilCompare_=STENCILCOMPARISONFUNCTION_ALWAYS;
    StencilOperation_t stencilPassOp_=STENCILOPERATION_KEEP,stencilFailOp_=STENCILOPERATION_KEEP,stencilDepthFailOp_=STENCILOPERATION_KEEP;
    float fogStart_=0, fogEnd_=1, fogZ_=0, fogMaxDensity_=1;
    ConVar *pixelFogConVar_=nullptr;
    int pixelFogRegister_=-1;
    bool fogDirty_=true,lastPixelFog_=false,lastFogSRGBWrite_=false;
    int lastFogHDR_=-1;
    std::array<float,4> rasterFogColor_{};
    float heightClipZ_=0;
    MaterialHeightClipMode_t heightClipMode_=MATERIAL_HEIGHTCLIPMODE_DISABLE;
    VMatrix userClipView_{};
    bool userClipViewOverride_=false;
    bool fogRadial_=false, disallowAccess_=false, mutexEnabled_=true;
    std::array<std::array<float,4>,6> worldClipPlanes_{};
    VMatrix cachedWorldToClip_{},cachedClipToWorld_{};
    bool clipInverseValid_=false;
    std::array<float,4> fastClipPlane_{};
    uint32_t clipPlaneMask_=0;
    bool fastClipEnabled_=false;
    bool stencilEnabled_=false, alphaToCoverage_=false;
    bool colorWriteOverride_=false, colorWriteOverrideValue_=true;
    bool alphaWriteOverride_=false, alphaWriteOverrideValue_=true;
    CThreadFastMutex shaderMutex_;
    std::array<float,64> fastFloatParams_{};
    std::array<int,64> fastIntParams_{};
    std::array<Vector,64> fastVectorParams_{};
    VertexShaderHandle_t boundVS_=VERTEX_SHADER_HANDLE_INVALID;
    GeometryShaderHandle_t boundGS_=GEOMETRY_SHADER_HANDLE_INVALID;
    PixelShaderHandle_t boundPS_=PIXEL_SHADER_HANDLE_INVALID;
    std::array<CMeshDX12 *,16> boundMeshes_{};
    std::array<VertexBindingDX12,16> boundVertexBuffers_{};
    std::array<VertexBindingDX12,16> drawMeshBindings_{};
    CIndexBufferDX12 *boundIndexBuffer_ = nullptr;
    size_t boundIndexOffset_ = 0;
    IMaterial *boundMaterial_=nullptr;
    CMeshDX12 *renderMesh_=nullptr;
    int renderFirstIndex_=0,renderIndexCount_=0;
    IMesh *dynamicMesh_=nullptr;
    CUtlVector<CMeshDX12 *> dynamicMeshes_;
    uint64_t frameCounter_=0;
    // Motion-vector pass (motion_vectors_dx12.cpp); contracts C2/C4/C7.
    MotionPassStateDX12 motionPassState_=MotionPassStateDX12::None;
    int motionPassSlot_=0;
    bool motionUnavailable_=false;
    uint64_t motionMainFrame_=~0ull;
    uint8_t motionWarned_=0;
    ShaderAPITextureHandle_t motionResolveTarget_=0;
    Microsoft::WRL::ComPtr<ID3D12Resource> motionTarget_;
    D3D12_RESOURCE_STATES motionTargetState_=D3D12_RESOURCE_STATE_RENDER_TARGET;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> motionRtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE motionRtv_{};
    UINT motionTargetWidth_=0,motionTargetHeight_=0,motionTargetSamples_=0,motionTargetQuality_=0;
    std::array<ShaderRecordDX12 *,2> motionVS_{};
    ShaderRecordDX12 *motionPS_=nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> motionReprojectRoot_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> motionReprojectPso_;
    UINT motionReprojectSamples_=0;
    std::array<std::array<float,16>,2> motionCurViewProj_{},motionPrevViewProj_{};
    std::array<bool,2> motionCurViewProjValid_{},motionPrevViewProjValid_{};
    dx12native::DX12MotionVS motionBlock_{};
    uint64_t motionBlockVersion_=0;
    int motionObjectKey_=0,motionLastObjectKey_=INT_MIN;
    uint32_t motionObjectOrdinal_=0;
    MotionHistoryTableDX12 motionHistory_[2];
    int motionHistoryCurrent_=0;
    uint32_t motionPassDraws_=0,motionPassObjects_=0,motionSuppressedPasses_=0;
    uint64_t motionLogFrame_=0;
    static constexpr int kMotionDepthBias=0;
    uint32_t frameDrawCount_=0;
    uint32_t frameFlushCount_=0;
    uint32_t frameSyncCount_=0;
    bool frameActive_=false;
    std::string lastToken_;
    std::unique_ptr<OcclusionQueryDX12> CreateOcclusionQuery();
    // Hash handles are only hints: validate occupancy and the complete key after any table mutation.
    std::array<UtlHashHandle_t,2> namedReferenceHints_{{decltype(namedShaderReferences_)::InvalidHandle(),decltype(namedShaderReferences_)::InvalidHandle()}};
    std::array<UtlHashHandle_t,2> namedComboHints_{{decltype(namedShaderCombos_)::InvalidHandle(),decltype(namedShaderCombos_)::InvalidHandle()}};
    // Per-snapshot named combo results. Bumping the epoch drops every entry; required whenever
    // combos are purged, snapshots are cleared or reference marks must be re-established.
    struct NamedResolveEntryDX12 { StateSnapshot_t snapshot=static_cast<StateSnapshot_t>(-1); int dynamicIndex=0; uint64_t epoch=0; ShaderRecordDX12 *record=nullptr; UtlHashHandle_t reference=0; uint64_t referenceEpoch=0; };
    // Bumped when reference marks are cleared; cached entries re-mark their reference before reuse.
    uint64_t namedReferenceEpoch_=1;
    std::array<std::array<NamedResolveEntryDX12,1024>,2> namedResolveCache_{};
    uint64_t namedResolveEpoch_=1;
    ShaderRecordDX12 *ResolveActiveNamedShader(bool pixel,int dynamicIndex);
    // Exact-input transform cache: recomputed only when view/projection bytes differ.
    VMatrix cachedTransformView_{},cachedTransformProjection_{},cachedViewProjection_{};
    Vector cachedCameraPosition_{0.f,0.f,0.f};
    bool cachedTransformValid_=false,cachedCameraValid_=false;
    // Exact-input fog cache. Zero-initialized before filling so memcmp covers padding.
    struct FogInputsDX12 { float start,end,z,density,camera[3],tone; int mode,passFog,hdr; unsigned char color[3]; bool pixelFog,srgbWrite,gammaDisabled; };
    FogInputsDX12 lastFogInputs_{};
    std::array<float,4> fogVertexParams_{},fogCameraParams_{},fogPixelParams_{},fogPixelColor_{};
    bool fogOutputsValid_=false;
    float toneScaleGamma_=1.f;
    bool toneScaleConstantValid_=false;
    // Render-target descriptions. The reference pins the resource, so pointer equality cannot alias a new object.
    struct CachedResourceDescDX12 {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource; D3D12_RESOURCE_DESC desc{};
        const D3D12_RESOURCE_DESC &Get(ID3D12Resource *value){if(resource.Get()!=value){resource=value;desc=value->GetDesc();}return desc;}
    };
    std::array<CachedResourceDescDX12,RenderTargetBindingDX12::kMaxColorTargets+1> targetDescs_{};
    // Recorded blits name their RTV until replay, so blits rotate through a ring (see BlitTexture).
    static constexpr int kBlitRtvSlots=64;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> blitRtvHeap_;
    int blitRtvSlot_=0;
};
extern CShaderAPIDX12 *g_pShaderAPIDX12;
} // namespace shaderapidx12
