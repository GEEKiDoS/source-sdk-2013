//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Native D3D12 implementation of IShaderAPI (dynamic state, textures, draws).
//
//=============================================================================//

#ifndef SHADERAPI_DX12_H
#define SHADERAPI_DX12_H
#pragma once

#include "materialsystem/shaderapidx12/native_cbuffer_dx12.h"
#include "materialsystem/shaderapidx12/native_engine_cbuffers_dx12.h"
#include "shaderapi/ishaderapi.h"
#include "shaderapi/ishaderapidx12.h"
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
#include "materialsystem/shaderapidx12/upscaler_dx12.h"
#include "materialsystem/shaderapidx12/mesh_dx12.h"
#include "materialsystem/shaderapidx12/pipeline_dx12.h"
#include "tier0/threadtools.h"
#include "tier1/utlhashtable.h"
#include "tier1/utlmap.h"
#include "tier1/utlstring.h"
#include "tier1/utlvector.h"
#include <d3d12.h>
#include <climits>

class IShaderUtil;
class ConVar;

namespace shaderapidx12
{
// Pointer-identity string from a process-lifetime intern pool (see InternShaderNameDX12).
struct InternedNameDX12
{
	const char *text = "";

	bool empty() const { return !*text; }

	const char *c_str() const { return text; }

	bool operator==( const InternedNameDX12 &other ) const { return text == other.text; }

	bool operator!=( const InternedNameDX12 &other ) const { return text != other.text; }
};

InternedNameDX12 InternShaderNameDX12( const char *pszName );

//-----------------------------------------------------------------------------
// Purpose: Dynamic shader state shared by the shader API (device, constants, matrices)
//-----------------------------------------------------------------------------
class CShaderDynamicDX12
{
public:
	void SetDevice( CShaderDeviceDX12 *pDevice ) { m_pDevice = pDevice; }

protected:
	CShaderDeviceDX12 *m_pDevice = nullptr;
	ShaderViewport_t m_Viewports[16]{};
	int m_nViewportCount = 0;
	float m_VsFloat[256][4]{};
	float m_PsFloat[224][4]{};
	int m_VsInt[16][4]{};
	int m_PsInt[16][4]{};
	bool m_VsBool[16]{};
	bool m_PsBool[16]{};
	uint64_t m_ConstantVersions[6] = { 1, 1, 1, 1, 1, 1 };

	// Material space-1 blocks written through the private pointer bridge (VS b2-b7, PS b1-b7); sticky like registers.
	struct NativeCBufferSlotDX12
	{
		CUtlVector<unsigned char> bytes;
		const dx12native::NativeCBufferLegacyMapDX12 *legacyMap = nullptr;
		uint64_t layoutHash = 0, version = 0;
		uint32_t byteSize = 0;
		bool written = false;
	};

	NativeCBufferSlotDX12 m_NativeVSBlocks[6];
	NativeCBufferSlotDX12 m_NativePSBlocks[7];
	uint64_t m_nNativeVSVersion = 1, m_nNativePSVersion = 1;
	// Engine-owned space-1 blocks for native records, rebuilt only when their legacy inputs change.
	dx12native::DX12VSEngine m_NativeVSEngine{};
	dx12native::DX12VSBones m_NativeVSBones{};
	dx12native::DX12PSEngine m_NativePSEngine{};
	uint64_t m_NativeVSEngineInputs[5]{};
	uint64_t m_nNativeVSEngineVersion = 1, m_nNativePSEngineVersion = 1;
	uint64_t m_ExtensionVersions[4]{};
	ShaderVertexExtensionDX12 m_PreviousVertexExtension{};
	uint32_t m_nVertexExtensionClipMask = 0;
	ShaderPixelExtensionDX12 m_PreviousPixelExtension{};
	ShaderBumpExtensionDX12 m_PreviousBumpExtension{};
	bool m_bBumpExtensionDirty = true;
	Vector m_RenderingVectors[64]{};
	uint64_t m_nFixedVSVersion = 1, m_nFixedPSVersion = 1;
	float m_RenderingFloats[64]{};
	int m_RenderingInts[64]{};
	ShaderAPITextureHandle_t m_StandardTextures[TEXTURE_MAX_STD_TEXTURES]{};
	VMatrix m_Matrices[MATERIAL_MODEL_MAX + 1];
	MaterialMatrixMode_t m_MatrixMode = MATERIAL_MODEL;
	Vector m_Color{ 1, 1, 1 };
	float m_flColorAlpha = 1.0f;
	Vector m_CameraPosition{};
	Vector m_ToneScale{ 1, 1, 1 };
	FlashlightState_t m_Flashlight{};
	ITexture *m_pFlashlightDepthTexture = nullptr;
	VMatrix m_FlashlightMatrix{};
	MaterialFogMode_t m_FogMode = MATERIAL_FOG_NONE;
	unsigned char m_FogColor[3] = { 0, 0, 0 };
	int m_nVertexShaderIndex = -1, m_nPixelShaderIndex = 0;
	int m_nBoneCount = 0;
	// Number of model-matrix rows needed by the motion history block. This is
	// independent from the vertex weight-channel count.
	int m_nMotionBoneRows = 1;
	bool m_bEditorMode = false, m_bMorphing = false;
	CUtlVector<BoxDeformation_t> m_Deformations;
	CUtlVector<VMatrix> m_MatrixStacks[MATERIAL_MODEL_MAX + 1];
};

//-----------------------------------------------------------------------------
// Purpose: The DX12 shader API: IShaderAPI, IDebugTextureInfo and IShaderAPIDX12 on top of the native device
//-----------------------------------------------------------------------------
class CShaderAPIDX12 final : public CShaderDynamicDX12, public IShaderAPI, public IDebugTextureInfo, public IShaderAPIDX12, public IShaderAPIDX12Compute
{
public:
	CShaderAPIDX12();
	~CShaderAPIDX12();
	void ProcessPendingTextureDeletes();
	void SetViewports( int nCount, const ShaderViewport_t *pViewports ) override;
	int GetViewports( ShaderViewport_t *pViewports, int nMax ) const override;
	double CurrentTime() const override;
	void GetLightmapDimensions( int *w, int *h ) override;
	MaterialFogMode_t GetSceneFogMode() override;
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
	void Color3fv( float const *pColor ) override;
	void Color4f( float r, float g, float b, float a ) override;
	void Color4fv( float const *pColor ) override;
	void Color3ub( unsigned char r, unsigned char g, unsigned char b ) override;
	void Color3ubv( unsigned char const *pColor ) override;
	void Color4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a ) override;
	void Color4ubv( unsigned char const *pColor ) override;
	void SetVertexShaderConstant( int var, float const *pVec, int numConst = 1, bool bForce = false ) override;
	void SetPixelShaderConstant( int var, float const *pVec, int numConst = 1, bool bForce = false ) override;
	void SetDefaultState() override;
	void GetWorldSpaceCameraPosition( float *pPos ) const override;
	int GetCurrentNumBones( void ) const override;
	int GetCurrentLightCombo( void ) const override;
	MaterialFogMode_t GetCurrentFogType( void ) const override;
	void SetTextureTransformDimension( TextureStage_t textureStage, int dimension, bool projected ) override;
	void DisableTextureTransform( TextureStage_t textureStage ) override;
	void SetBumpEnvMatrix( TextureStage_t textureStage, float m00, float m01, float m10, float m11 ) override;
	void SetVertexShaderIndex( int vshIndex = -1 ) override;
	void SetPixelShaderIndex( int pshIndex = 0 ) override;
	void GetBackBufferDimensions( int &width, int &height ) const override;
	int GetMaxLights( void ) const override;
	const LightDesc_t &GetLight( int lightNum ) const override;
	void SetPixelShaderFogParams( int reg ) override;
	void SetVertexShaderStateAmbientLightCube() override;
	void SetPixelShaderStateAmbientLightCube( int pshReg, bool bForceToBlack = false ) override;
	void CommitPixelShaderLighting( int pshReg ) override;
	CMeshBuilder *GetVertexModifyBuilder() override;
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
	void SetFloatRenderingParameter( int parm_number, float value ) override;
	void SetStencilEnable( bool onoff ) override;
	void SetStencilFailOperation( StencilOperation_t op ) override;
	void SetStencilZFailOperation( StencilOperation_t op ) override;
	void SetStencilPassOperation( StencilOperation_t op ) override;
	void SetStencilCompareFunction( StencilComparisonFunction_t cmpfn ) override;
	void SetStencilReferenceValue( int ref ) override;
	void SetStencilTestMask( uint32 msk ) override;
	void SetStencilWriteMask( uint32 msk ) override;
	void ClearStencilBufferRectangle( int xmin, int ymin, int xmax, int ymax, int value ) override;
	void GetDXLevelDefaults( uint &max_dxlevel, uint &recommended_dxlevel ) override;
	const FlashlightState_t &GetFlashlightStateEx( VMatrix &worldToTexture, ITexture **pFlashlightDepthTexture ) const override;
	float GetAmbientLightCubeLuminance() override;
	void GetDX9LightState( LightState_t *state ) const override;
	int GetPixelFogCombo() override;
	void BindStandardVertexTexture( VertexTextureSampler_t sampler, StandardTextureId_t id ) override;
	bool IsHWMorphingEnabled() const override;
	void GetStandardTextureDimensions( int *pWidth, int *pHeight, StandardTextureId_t id ) override;
	void SetBooleanVertexShaderConstant( int var, BOOL const *pVec, int numBools = 1, bool bForce = false ) override;
	void SetIntegerVertexShaderConstant( int var, int const *pVec, int numIntVecs = 1, bool bForce = false ) override;
	void SetBooleanPixelShaderConstant( int var, BOOL const *pVec, int numBools = 1, bool bForce = false ) override;
	void SetIntegerPixelShaderConstant( int var, int const *pVec, int numIntVecs = 1, bool bForce = false ) override;
	bool ShouldWriteDepthToDestAlpha( void ) const override;
	void PushDeformation( DeformationBase_t const *Deformation ) override;
	void PopDeformation() override;
	int GetNumActiveDeformations() const override;
	void ExecuteCommandBuffer( uint8 *pCmdBuffer ) override;
	void SetStandardTextureHandle( StandardTextureId_t nId, ShaderAPITextureHandle_t nHandle ) override;
	int GetPackedDeformationInformation( int nMaskOfUnderstoodDeformations, float *pConstantValuesOut, int nBufferSize, int nMaximumDeformations, int *pNumDefsOut ) const override;
	void MarkUnusedVertexFields( unsigned int nFlags, int nTexCoordCount, bool *pUnusedTexCoords ) override;
	void GetCurrentColorCorrection( ShaderColorCorrectionInfo_t *pInfo ) override;
	void SetPSNearAndFarZ( int pshReg ) override;
	void SetDepthFeatheringPixelShaderConstant( int iConstant, float fDepthBlendScale ) override;
	int GetPixelFogCombo1( bool bSupportsRadial ) override;
	void ClearBuffers( bool bClearColor, bool bClearDepth, bool bClearStencil, int renderTargetWidth, int renderTargetHeight ) override;
	void ClearColor3ub( unsigned char r, unsigned char g, unsigned char b ) override;
	void ClearColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a ) override;
	void BindVertexShader( VertexShaderHandle_t hVertexShader ) override;
	void BindGeometryShader( GeometryShaderHandle_t hGeometryShader ) override;
	void BindPixelShader( PixelShaderHandle_t hPixelShader ) override;
	void SetRasterState( const ShaderRasterState_t &state ) override;
	bool SetMode( void *hwnd, int nAdapter, const ShaderDeviceInfo_t &info ) override;
	void ChangeVideoMode( const ShaderDeviceInfo_t &info ) override;
	StateSnapshot_t TakeSnapshot() override;
	void TexMinFilter( ShaderTexFilterMode_t texFilterMode ) override;
	void TexMagFilter( ShaderTexFilterMode_t texFilterMode ) override;
	void TexWrap( ShaderTexCoordComponent_t coord, ShaderTexWrapMode_t wrapMode ) override;
	void CopyRenderTargetToTexture( ShaderAPITextureHandle_t textureHandle ) override;
	void Bind( IMaterial *pMaterial ) override;
	void FlushBufferedPrimitives() override;
	IMesh *GetDynamicMesh( IMaterial *pMaterial, int nHWSkinBoneCount, bool bBuffered = true, IMesh *pVertexOverride = 0, IMesh *pIndexOverride = 0 ) override;
	IMesh *GetDynamicMeshEx( IMaterial *pMaterial, VertexFormat_t vertexFormat, int nHWSkinBoneCount, bool bBuffered = true, IMesh *pVertexOverride = 0, IMesh *pIndexOverride = 0 ) override;
	bool IsTranslucent( StateSnapshot_t id ) const override;
	bool IsAlphaTested( StateSnapshot_t id ) const override;
	bool UsesVertexAndPixelShaders( StateSnapshot_t id ) const override;
	bool IsDepthWriteEnabled( StateSnapshot_t id ) const override;
	VertexFormat_t ComputeVertexFormat( int numSnapshots, StateSnapshot_t *pIds ) const override;
	VertexFormat_t ComputeVertexUsage( int numSnapshots, StateSnapshot_t *pIds ) const override;
	void BeginPass( StateSnapshot_t snapshot ) override;
	void RenderPass( int nPass, int nPassCount ) override;
	void SetNumBoneWeights( int numBones ) override;
	void SetLight( int lightNum, const LightDesc_t &desc ) override;
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
	void TexImageFromVTF( IVTFTexture *pVTF, int iVTFFrame ) override;
	bool TexLock( int level, int cubeFaceID, int xOffset, int yOffset, int width, int height, CPixelWriter &writer ) override;
	void TexUnlock() override;
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
	void SelectionBuffer( unsigned int *pBuffer, int size ) override;
	void ClearSelectionNames() override;
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
	// Logical intervals survive Submit; every native interval is contained in one command list.
	void FinishOcclusionQueriesForSubmit();
	void ResumeOcclusionQueriesAfterSubmit();
	void StopOcclusionQueriesForShutdown();
	void SetFlashlightState( const FlashlightState_t &state, const VMatrix &worldToTexture ) override;
	void ClearVertexAndPixelShaderRefCounts() override;
	void PurgeUnusedVertexAndPixelShaders() override;
	void DXSupportLevelChanged() override;
	void EnableUserClipTransformOverride( bool bEnable ) override;
	void UserClipTransform( const VMatrix &worldToView ) override;
	MorphFormat_t ComputeMorphFormat( int numSnapshots, StateSnapshot_t *pIds ) const override;
	void SetRenderTargetEx( int nRenderTargetID, ShaderAPITextureHandle_t colorTextureHandle = SHADER_RENDERTARGET_BACKBUFFER, ShaderAPITextureHandle_t depthTextureHandle = SHADER_RENDERTARGET_DEPTHBUFFER ) override;
	void CopyRenderTargetToTextureEx( ShaderAPITextureHandle_t textureHandle, int nRenderTargetID, Rect_t *pSrcRect = NULL, Rect_t *pDstRect = NULL ) override;
	void CopyTextureToRenderTargetEx( int nRenderTargetID, ShaderAPITextureHandle_t textureHandle, Rect_t *pSrcRect = NULL, Rect_t *pDstRect = NULL ) override;
	void HandleDeviceLost() override;
	void EnableLinearColorSpaceFrameBuffer( bool bEnable ) override;
	void SetFullScreenTextureHandle( ShaderAPITextureHandle_t h ) override;
	void SetIntRenderingParameter( int parm_number, int value ) override;
	void SetVectorRenderingParameter( int parm_number, Vector const &value ) override;
	float GetFloatRenderingParameter( int parm_number ) const override;
	int GetIntRenderingParameter( int parm_number ) const override;
	Vector GetVectorRenderingParameter( int parm_number ) const override;
	void SetFastClipPlane( const float *pPlane ) override;
	void EnableFastClip( bool bEnable ) override;
	void GetMaxToRender( IMesh *pMesh, bool bMaxUntilFlush, int *pMaxVerts, int *pMaxIndices ) override;
	int GetMaxVerticesToRender( IMaterial *pMaterial ) override;
	int GetMaxIndicesToRender() override;
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
	void ComputeVertexDescription( unsigned char *pBuffer, VertexFormat_t vertexFormat, MeshDesc_t &desc ) const override;
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
	void SetFlexWeights( int nFirstWeight, int nCount, const MorphWeight_t *pWeights ) override;
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
	void LockRect( void **pOutBits, int *pOutPitch, ShaderAPITextureHandle_t texHandle, int mipmap, int x, int y, int w, int h, bool bWrite, bool bRead ) override;
	void UnlockRect( ShaderAPITextureHandle_t texHandle, int mipmap ) override;
	void TexLodClamp( int finest ) override;
	void TexLodBias( float bias ) override;
	void CopyTextureToTexture( ShaderAPITextureHandle_t srcTex, ShaderAPITextureHandle_t dstTex ) override;
	int VertexFormatSize( VertexFormat_t vertexFormat ) const override;
	void SceneFogRadial( bool bRadial ) override;
	bool GetSceneFogRadial() override;

	void SetShaderUtil( IShaderUtil *pShaderUtil ) { m_pShaderUtil = pShaderUtil; }

	bool InitializeDeviceResources( CShaderDeviceDX12 *pDevice );
	void ShutdownDeviceResources();
	// Development-only shader_precache: any thread queues; the recording owner validates in BeginFrame.
	void QueueShaderPrecacheRequest( const char *pszName, int nStaticIndex, int nDynamicIndex );
	void ProcessShaderPrecacheRequests();
	void SetShaderPrecacheAccepting( bool bAccepting );
	void ApplyNativeCBufferWrite( const dx12native::NativeCBufferWriteDX12 &write, bool bPixel );

	struct PrecacheRequestDX12
	{
		CUtlString name;
		int staticIndex = -1, dynamicIndex = -1;
	};

	CThreadFastMutex m_PrecacheMutex;
	CUtlVector<PrecacheRequestDX12> m_PrecacheRequests;
	bool m_bPrecacheAccepting = false;
	// IDebugTextureInfo
	void EnableDebugTextureList( bool bEnable ) override;
	void EnableGetAllTextures( bool bEnable ) override;
	KeyValues *GetDebugTextureList() override;
	int GetTextureMemoryUsed( TextureMemoryType eTextureMemory ) override;
	bool IsDebugTextureListFresh( int numFramesAllowed = 1 ) override;
	bool SetDebugTextureRendering( bool bEnable ) override;
	void DrawMesh( CMeshDX12 *pMesh, int nFirstIndex, int nIndexCount );
	void DrawMaterialMesh( CMeshDX12 *pMesh, int nFirstIndex, int nIndexCount );
	void RetireShaderPipelines( ShaderRecordDX12 *pRecord );
	void ResetNativeState();
	ShaderRecordDX12 *ResolveNamedShader( const char *pszName, bool bPixel, int nStaticIndex, int nDynamicIndex );
	ShaderRecordDX12 *ResolveComputeShader( const char *pszName, int nStaticIndex, int nDynamicIndex );
	bool PrepareSampledTexture( ShaderAPITextureHandle_t hTexture, bool bSRGB, ID3D12Resource **ppResource, D3D12_SHADER_RESOURCE_VIEW_DESC &srv, D3D12_SAMPLER_DESC &sampler, D3D12_CPU_DESCRIPTOR_HANDLE *pSource = nullptr, bool bComparison = false, int nFirstMip = -1, int nMipCount = 0 );
	bool PrepareRenderTargets( RenderTargetBindingDX12 &binding, bool bEncodeSRGB = true );
	void ReleaseTextureDeviceResources();
	enum class BlitEncodeDX12 : uint8_t { None, Gamma, HdrScale };
	// Presentation encode of the scene into a scene-sized FP16 or R8G8B8A8_UNORM texture (textures_dx12.cpp).
	bool EncodeSceneTo( ID3D12Resource *pDestination, D3D12_RESOURCE_STATES &destinationState );
	float PresentOutputScale() const;
	bool EnsureComputeRootSignature();
	struct TextureRecord;
	bool PromoteRenderTarget( TextureRecord &texture, bool bUav, int nMipLevels );
	void ResizeTextureStaging( TextureRecord &texture, int nMipLevels );

	int StencilReference() const { return m_nStencilRef; }
	// Frame generation hooks for the device's Present (see shaderdevice_dx12.cpp).
	bool FrameGenDispatchedThisFrame() const { return m_nFrameGenQueuedFrame == m_nFrameCounter; }

	uint32_t FrameGenFrameId() const { return m_nFrameGenLatchedFrameId; }

	// Depth convention of the current perspective projection (false when it is not a perspective matrix).
	bool ProjectionIsInverted() const;

	uint8_t StencilReadMask() const { return m_nStencilReadMask; }

	StencilComparisonFunction_t StencilCompare() const { return m_StencilCompare; }

#if defined( SHADERAPI_DX12_SMOKE )
	// Smoke host only: observes resource-heap rollover for the shadowmaps descriptor-cache checks.
	uint64_t SmokeResourceHeapGeneration() const { return m_Pipeline.ResourceHeapGeneration(); }
	// Resolve through the ordinary draw path, then observe the selected original-key entry.
	bool SmokeInspectComboFold( bool pixel, ComboFoldProjectionDX12 &projection ) const
	{
		const auto *combo = m_BoundNamedCombos[pixel ? 1 : 0];
		if ( !combo || !combo->folded || !combo->Record() ) return false;
		projection = combo->projection;
		return true;
	}
	// Substitute only the test program, preserving the logical's payload and engine-ring upload.
	bool SmokeBindComboFoldProbe( PixelShaderHandle_t probe )
	{
		if ( !m_BoundNamedCombos[1] || !m_BoundNamedCombos[1]->folded ) return false;
		m_hBoundPS = probe;
		m_bBoundPixelShaderIsNamed = true;
		m_bNamedPixelShaderDirty = false;
		return true;
	}
#endif

	friend bool PrepareSampledTextureDX12( CShaderAPIDX12 &, ShaderAPITextureHandle_t, bool, ID3D12Resource **, D3D12_SHADER_RESOURCE_VIEW_DESC &, D3D12_SAMPLER_DESC &, D3D12_CPU_DESCRIPTOR_HANDLE *, bool, int, int );
	friend bool PrepareRenderTargetsDX12( CShaderAPIDX12 &, RenderTargetBindingDX12 &, bool );
	friend class CLightingDX12;
	friend class CHighresLightmapsDX12;

	struct TextureRecord
	{
		ShaderAPITextureHandle_t id = 0;
		uint64 allocationSerial = 0;
		uint32 highresPage = 0xffffffffu;
		uint64 highresLayoutGeneration = 0;
		int width = 0, height = 0, depth = 1, mipLevels = 1, copies = 1;
		int currentCopy = 0;
		bool switchNeeded = false;
		size_t bytesPerCopy = 0;
		ImageFormat format = IMAGE_FORMAT_UNKNOWN, requestedFormat = IMAGE_FORMAT_UNKNOWN;
		int flags = 0, priority = 0, binds = 0;
		CUtlString name;
		CUtlVector<unsigned char> pixels;
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		bool uavCapable = false; // created with ALLOW_UNORDERED_ACCESS (render targets promoted by compute)

		struct ResourceCopy
		{
			Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		};

		CUtlVector<ResourceCopy> resources;
		D3D12_CPU_DESCRIPTOR_HANDLE m_SrvSources[2]{};
		ID3D12Resource *m_pSrvResources[2]{};
		D3D12_SHADER_RESOURCE_VIEW_DESC m_SrvDescriptors[2]{};
		D3D12_SAMPLER_DESC m_SamplerDescriptors[2]{};
		bool m_SamplerDescriptorValid[2]{};
		int m_nSamplerDescriptorAnisotropy = -1;
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
		int minFilter = 1, magFilter = 1;
		int wrapU = 1, wrapV = 1, wrapW = 1;
		float lodBias = 0;
		int lodClamp = 0;
		bool gpuDirty = true;
	};

	struct PreparedTextureSlot
	{
		ShaderAPITextureHandle_t handle = 0;
		TextureRecord *record = nullptr;
		ID3D12Resource *resource = nullptr;
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		D3D12_SAMPLER_DESC sampler{};
		D3D12_CPU_DESCRIPTOR_HANDLE source{};
		uint64_t epoch = 0;
		uint16_t samplerId = 0;
		bool srgb = false, comparison = false, valid = false;
	};

	PreparedTextureSlot m_PreparedTextureSlots[32];

	struct TextureSetKeyDX12
	{
		ShaderAPITextureHandle_t pixel[16];
		ShaderAPITextureHandle_t vertex[4];
		uint64_t epoch, fence, nullView;
		uint32_t sampledMask, srgbMask, comparisonMask;
	};

	TextureSetKeyDX12 m_LastTextureSet{};
	bool m_bTextureSetValid = false;
	// Bumped by every texture change that can affect a prepared sampled slot.
	uint64_t m_nTextureStateEpoch = 1;
	// Bumped when a texture's resource, copy or views change identity (create, rotate, release, delete).
	uint64_t m_nTextureIdentityEpoch = 1;

	struct RenderTargetCacheDX12
	{
		bool valid = false, srgb = false;
		uint64_t identityEpoch = 0;
		ShaderAPITextureHandle_t depthTarget = 0;
		ShaderAPITextureHandle_t handles[RenderTargetBindingDX12::kMaxColorTargets]{};
		ID3D12Resource *sceneColor = nullptr, *sceneDepth = nullptr;
		SIZE_T sceneRtv = 0;
		RenderTargetBindingDX12 binding{};
		TextureRecord *records[RenderTargetBindingDX12::kMaxColorTargets]{};
		TextureRecord *depth = nullptr;
	} m_RenderTargetCache;

	DescriptorRangeDX12 m_PreparedSamplerTable{};
	uint64_t m_nPreparedSamplerFence = 0;
	uint32_t m_nPreparedSamplerMask = 0;

	struct InputLayoutCache
	{
		ShaderRecordDX12 *shader = nullptr;
		uint64_t variant = 0, policy = 0, layoutKey = 0;
		VertexFormat_t format = 0;
		uint32_t instanceCount = 0;
		uint8_t streamFlags = 0;
		UINT count = 0;
		bool zeroInput = false, valid = false;
		D3D12_INPUT_ELEMENT_DESC elements[MAX_VERTEX_INPUTS_DX12]{};
	};

	// Direct-mapped by shader/variant/format/policy; entries whose shader is retired are invalidated.
	InputLayoutCache m_InputLayoutCaches[64];
	// Draw-to-draw reused binding input; slots outside m_nDrawBindingMask always hold the null view.
	CPipelineCacheDX12::BindingInputDX12 m_DrawBindingInput{ D3D12_CPU_DESCRIPTOR_HANDLE{} };
	D3D12_CPU_DESCRIPTOR_HANDLE m_DrawBindingNull{};
	uint32_t m_nDrawBindingMask = 0;
	// Bound-texture dimension per slot; handle values are never reused and DeleteTexture clears matches.
	ShaderAPITextureHandle_t m_TextureTypeHandles[16]{};
	uint8_t m_TextureTypeValues[16]{};

	// Every input of the pipeline-selection part of DrawBuffers (translation, input layout, PSO).
	// Zero-filled before assignment so memcmp is exact.
	struct PipelineSignatureDX12
	{
		uint64_t resolveEpoch, vs, ps, gs, format, layoutKey, policy;
		uint32_t motionPass;
		int64_t snapshot;
		uint64_t textureTypes;
		DXGI_FORMAT colorFormats[RenderTargetBindingDX12::kMaxColorTargets];
		DXGI_FORMAT depthFormat;
		uint32_t instanceCount, primitive, streamFlags, clipMask, colorCount, samples, quality, hasDepth;
		ShaderRasterState_t rasterState;
		uint32_t rasterOverride, shadeMode, fogMode, pixelFog, cullMode;
		uint32_t stencilEnabled, stencilCompare, stencilFail, stencilDepthFail, stencilPass, stencilReadMask, stencilWriteMask;
		uint32_t alphaToCoverage, colorWriteOverride, colorWriteValue, alphaWriteOverride, alphaWriteValue, overrideDepthEnable, overrideDepthValue, forceDepthEquals, reverseDepth;
		float shadowSlope, shadowDepth, slopeDecal, slopeNormal, depthDecal, depthNormal;
	};

	struct PipelineMemoDX12
	{
		PipelineSignatureDX12 signature{};
		ShaderRecordDX12 *vs = nullptr, *ps = nullptr;
		ID3D12PipelineState *pso = nullptr;
		uint64_t vsVariant = 0, psVariant = 0, epoch = 0, psoEpoch = 0;
		bool depthOnly = false, generatedVS = false, generatedPS = false, zeroInput = false, geometryStage = false;
	};

	struct DrawStatsDX12
	{
		uint64_t draws = 0, memoHits = 0;
	} m_DrawStats;

	uint32_t m_nDrawStatsFrames = 0;
	// Direct-mapped pipeline memo. Entries are valid for the current epoch, which is bumped whenever
	// shader records, snapshots or the device go away; PSO lifetime is tracked by the pipeline epoch.
	PipelineMemoDX12 m_PipelineMemos[256];
	uint64_t m_nPipelineMemoEpoch = 1;
	// Last translation state key and the exact inputs that produced it.
	ShaderRasterStateDX12 m_TranslationKeyMemoRaster{};
	uint64_t m_nTranslationKeyMemoLayout = 0, m_nTranslationKeyMemo = 0;
	bool m_bTranslationKeyMemoValid = false;

private:
	struct VertexBindingDX12
	{
		CVertexBufferDX12 *buffer = nullptr;
		uint32_t byteOffset = 0, firstVertex = 0, vertexCount = 0, repetitions = 1;
		VertexFormat_t format = 0;
	};

	void DrawBuffers( const VertexBindingDX12 ( &streams )[16], CIndexBufferDX12 *pIndices,
	    size_t nIndexOffset, MaterialPrimitiveType_t primitiveType, int nFirstIndex, int nIndexCount,
	    bool bMeshStreams = false, uint64 meshToken = 0, const CMeshDX12 *mesh = nullptr );
	void SetMotionPass( int nMode );
	bool EnsureMotionResources();
	bool PrepareMotionBinding( RenderTargetBindingDX12 &binding );
	void DrawMotionReprojection();
	void ResolveMotionTarget();
	ShaderRecordDX12 *MotionVertexShader( VertexFormat_t vertexFormat );
	void FillMotionBlock( const VertexBindingDX12 &vertexBinding, CIndexBufferDX12 *pIndexBuffer, size_t nIndexOffset, int nFirstIndex, int nIndexCount );
	void MarkMotionTargetStale();

	bool MotionPassActive() const { return m_MotionPassState == MotionPassStateDX12::Active; }

	void ReleaseMotionResources();
	void TransitionMotionTarget( D3D12_RESOURCE_STATES desiredState );
	// PBR G-buffer targets (gbuffer_dx12.cpp).
	void SetGBufferPass( int nMode );
	bool EnsureGBufferResources();
	void TransitionGBuffer( D3D12_RESOURCE_STATES desiredState );
	void ReleaseGBufferResources();
	// Native-AA upscaler (upscaler_dx12.cpp); contract U1-U4 of the upscaler plan.
	void SetUpscalerMode( int nMode );
	void DispatchUpscaler( int nFlags );
	void ConsumeUpscalerReplays( bool bWait );
	bool EnsureUpscalerOutput( UINT nWidth, UINT nHeight );
	void ReleaseUpscalerFeature();
	// DLSS-NR chain after the temporal AA, configured by INT_RENDERPARM_DX12_NR_CONFIG and the NR float parameters.
	// Returns the layer count this dispatch runs (0 when off, unavailable or failed; status says which).
	uint32_t PrepareUpscalerNr( DlssNrComposeDX12 &compose );
	void ReleaseUpscalerNr();
	void ReleaseIdleUpscaler();
	bool WaitUpscalerGpuIdle();
	void ReleaseUpscalerResources();
	// Frame generation (upscaler_dx12.cpp); contract F1-F10 of the frame-generation plan. The provider object is
	// owned by the device; the shader API gates the per-frame dispatch and publishes status.
	void ApplyFrameGenSettings(); // BeginFrame: takes over the IShaderAPIDX12 requests on the recording thread
	void SetFrameGenMode( int nMode, int nMultiplier, bool bHudless );
	void SetFrameGenView( int nEligible );
	void SetReflexRequest( int nMode );
	void EnsureFrameGenInitialized();
	void DispatchFrameGen( int nFlags );
	void ConsumeFrameGenReplays( bool bWait );
	void ReleaseFrameGenResources();

	// IShaderAPIDX12 and IShaderAPIDX12Compute
	void SetFrameGeneration( int nMode, int nMultiplier, bool bHudless ) override;
	void SetReflexMode( int nMode ) override;
	void SetFrameRateLimit( float flFps ) override;
	void LatencyMarker( int nMarker, unsigned int nFrameId ) override;
	int FrameGenerationStatus() override { return m_nFrameGenStatus; }
	int FramesShown() override { return m_nFramesShown; }
	int ReflexStatus() override { return m_nReflexStatus; }
	const char *LastError() override { return m_szFrameGenError; }
	void SetHdrOutput( bool bEnable, float flUiNits ) override;
	int HdrDisplayStatus() override;
	bool HdrOutputCapable() override;
	bool Dispatch( const ShaderAPIDX12ComputeDispatch_t &dispatch ) override;
	int SceneSampleCount() override;
	void SampleUpscalerJitter();

	// Jitter goes only into eligible perspective scene draws (or the motion pass) before this frame's dispatch.
	bool UpscalerJitterActive( bool bMotionActive ) const
	{
		return m_pDevice && !m_pDevice->Lighting().ShadowPassActive() && m_bUpscalerViewEligible && m_nUpscalerFrameToken == m_nFrameCounter && m_nUpscalerQueuedFrame != m_nFrameCounter &&
		    ( m_UpscalerJitter[0] != 0.f || m_UpscalerJitter[1] != 0.f ) && ( bMotionActive || m_RenderTargets[0] == SHADER_RENDERTARGET_BACKBUFFER ) &&
		    m_Matrices[MATERIAL_PROJECTION][3][3] == 0.f && m_Matrices[MATERIAL_PROJECTION][3][2] != 0.f && m_pDevice && m_pDevice->SceneSampleCount() == 1;
	}

	CMeshBuilder m_VertexModifyBuilder;

	struct NamedShaderKeyView
	{
		const char *name;
		int staticIndex, dynamicIndex;
		bool pixel;
	};

	struct NamedShaderKey
	{
		CUtlString name;
		int staticIndex, dynamicIndex;
		bool pixel;

		NamedShaderKey( NamedShaderKeyView key )
		    : name( key.name ), staticIndex( key.staticIndex ), dynamicIndex( key.dynamicIndex ), pixel( key.pixel ) {}

		operator NamedShaderKeyView() const { return { name.String(), staticIndex, dynamicIndex, pixel }; }
	};

	struct NamedShaderHash
	{
		unsigned operator()( NamedShaderKeyView key ) const
		{
			unsigned hash = StringHashFunctor()( key.name );
			hash = Mix32HashFunctor()( hash ^ static_cast<uint32>( key.staticIndex ) );
			hash = Mix32HashFunctor()( hash ^ static_cast<uint32>( key.dynamicIndex ) );
			return Mix32HashFunctor()( hash ^ static_cast<uint32>( key.pixel ) );
		}
	};

	struct NamedShaderEqual
	{
		bool operator()( NamedShaderKeyView a, NamedShaderKeyView b ) const
		{
			return a.staticIndex == b.staticIndex && a.dynamicIndex == b.dynamicIndex && a.pixel == b.pixel && StringEqualFunctor()( a.name, b.name );
		}
	};

	CUtlHashtable<NamedShaderKey, bool, NamedShaderHash, NamedShaderEqual, NamedShaderKeyView> m_NamedShaderReferences;
	CUtlHashtable<CUtlString, float, StringHashFunctor, StringEqualFunctor, const char *> m_Knobs;
	unsigned int m_nUnusedVertexFields = 0;
	bool m_UnusedTextureCoordinates[VERTEX_MAX_TEXTURE_COORDINATES]{};
	void MatrixChanged();
	void CommitTransforms();
	bool ShouldUsePixelFog() const;
	void CommitFogState();
	bool m_bTransformsDirty = true;

	struct SourceLayoutEntryDX12
	{
		VertexFormat_t format = 0;
		uint8_t flags = 0;
		bool valid = false;
		uint64_t translationKey = 0;
		VertexLayoutDX12 layout{};
	};

	SourceLayoutEntryDX12 m_SourceLayouts[16];
	uint64_t m_nSourceLayoutTranslationKey = 0;
	int m_nMaxBoneLoaded = 0;
	int SortLocalLights( int ( &indices )[4] ) const;
	void CommitVertexLighting();
	LightDesc_t m_Lights[4]{};
	float m_AmbientCube[6][4]{};
	Vector m_LightingOrigin{ 0.f, 0.f, 0.f };
	bool m_bLightingDirty = false;
	bool m_bNamedVertexShaderDirty = false, m_bNamedPixelShaderDirty = false;
	bool m_bBoundVertexShaderIsNamed = false, m_bBoundPixelShaderIsNamed = false;
	bool EnsureTextureResident( TextureRecord &texture );
	bool AllocateNativeTexture( TextureRecord &texture );
	bool RefreshTextureStaging( TextureRecord &texture, int nFace, int nMip );
	void AdvanceTextureCopy( TextureRecord &texture );

	struct Snapshot
	{
		bool translucent = false, alphaTest = false, depthWrite = true, depthTest = true, shaders = false, colorWrites = true, alphaWrites = true, culling = true, stencil = false, alphaToCoverage = false, separateAlpha = false, srgbWrite = false, fogGammaDisabled = false;
		ShaderFogMode_t fogMode = SHADER_FOGMODE_DISABLED;
		float alphaReference = 0.f;
		ShaderAlphaFunc_t alphaFunction = SHADER_ALPHAFUNC_ALWAYS;
		ShaderBlendFactor_t blendSource = SHADER_BLEND_ONE, blendDestination = SHADER_BLEND_ZERO, blendAlphaSource = SHADER_BLEND_ONE, blendAlphaDestination = SHADER_BLEND_ZERO;
		ShaderBlendOp_t blendOperation = SHADER_BLEND_OP_ADD, blendAlphaOperation = SHADER_BLEND_OP_ADD;
		ShaderStencilFunc_t stencilFunction = SHADER_STENCILFUNC_ALWAYS;
		ShaderStencilOp_t stencilFail = SHADER_STENCILOP_KEEP, stencilDepthFail = SHADER_STENCILOP_KEEP, stencilPass = SHADER_STENCILOP_KEEP;
		uint8_t stencilReadMask = 0xff, stencilWriteMask = 0xff, stencilReference = 0;
		ShaderDepthFunc_t depthFunction = SHADER_DEPTHFUNC_NEAREROREQUAL;
		VertexFormat_t format = 0, usage = 0;
		InternedNameDX12 vertexShaderName, pixelShaderName;
		int staticVertexIndex = 0, staticPixelIndex = 0;
		MorphFormat_t morph = 0;
		uint16_t comparisonSamplerMask = 0, srgbReadMask = 0;
		FixedFunctionStateDX12 fixed{};
		PolygonOffsetMode_t polygonOffset = SHADER_POLYOFFSET_DISABLE;
		ShaderPolyMode_t polyFront = SHADER_POLYMODE_FILL, polyBack = SHADER_POLYMODE_FILL;

		uint64_t Fingerprint() const;
		bool Matches( const Snapshot &other ) const;
		// One field list feeds both hash and equality; no padding or draw-time cache state.
		template <typename Visitor> void VisitIdentity( const Snapshot &other, Visitor &&visit ) const;
	};

	CThreadFastMutex m_StateMutex;
	CPipelineCacheDX12 m_Pipeline;
	D3D12_VIEWPORT m_PrivateShadowViewport{};
	D3D12_RECT m_PrivateShadowScissor{};

	struct ClearPassDX12
	{
		DXGI_FORMAT color = DXGI_FORMAT_UNKNOWN, depth = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT colors[RenderTargetBindingDX12::kMaxColorTargets]{};
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

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pClearRoot;
	CUtlVector<ClearPassDX12> m_ClearPasses;
	void DrawMaskedClear( bool bRGB, bool bAlpha, bool bDepth, const D3D12_RECT *pRect = nullptr, bool bStencilOnly = false );

	struct BlitPassDX12
	{
		DXGI_FORMAT color = DXGI_FORMAT_UNKNOWN;
		UINT samples = 1;
		UINT quality = 0;
		BlitEncodeDX12 encode = BlitEncodeDX12::None;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
	};

	struct ResolveTextureRecord
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		D3D12_RESOURCE_DESC desc{};
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RESOLVE_DEST;
		uint64_t lastUseFence = 0;
	};

	ResolveTextureRecord *AcquireResolveTexture( const D3D12_RESOURCE_DESC &desc, HRESULT &hrCreation );
	CUtlVector<ResolveTextureRecord *> m_ResolveTextures;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pBlitRoot;
	CUtlVector<BlitPassDX12> m_BlitPasses;
	bool BlitTexture( ID3D12Resource *pSource, D3D12_RESOURCE_STATES &sourceState, bool bSourceIsScene, ImageFormat sourceFormat, ID3D12Resource *pDestination, D3D12_RESOURCE_STATES &destinationState, bool bDestinationIsScene, ImageFormat destinationFormat, Rect_t sourceRect, Rect_t destinationRect, bool bSourceSRGB, bool bDestinationSRGB, const float *pGammaCoefficients = nullptr, BlitEncodeDX12 encode = BlitEncodeDX12::None );
	void CopyTextureRegionDX12( ShaderAPITextureHandle_t hSource, ShaderAPITextureHandle_t hDestination, Rect_t *pSourceRect, Rect_t *pDestinationRect );
	struct OcclusionQueryDX12;
	CUtlVector<OcclusionQueryDX12 *> m_OcclusionQueries;
	CUtlVector<OcclusionQueryDX12 *> m_ActiveOcclusionQueries;
	bool AddOcclusionQuerySegment( OcclusionQueryDX12 *query );
	bool BeginOcclusionQuerySegment( OcclusionQueryDX12 *query );
	void EndOcclusionQuerySegment( OcclusionQueryDX12 *query );
	void CollectOcclusionQuerySegments( OcclusionQueryDX12 *query, uint64_t completed );
	CUtlHashtable<ShaderAPITextureHandle_t, TextureRecord *> m_Textures;

	// Handles are never reused, so a direct-mapped cache of live records only needs clearing on deletion.
	struct TextureLookupDX12
	{
		ShaderAPITextureHandle_t handle = 0;
		TextureRecord *record = nullptr;
	};

	mutable TextureLookupDX12 m_TextureLookup[256];

	TextureRecord *FindTexture( ShaderAPITextureHandle_t hTexture ) const
	{
		TextureLookupDX12 &cached = m_TextureLookup[static_cast<size_t>( hTexture ) & ( ARRAYSIZE( m_TextureLookup ) - 1 )];
		if ( cached.record && cached.handle == hTexture )
			return cached.record;
		UtlHashHandle_t hEntry = m_Textures.Find( hTexture );
		if ( hEntry == m_Textures.InvalidHandle() )
			return nullptr;
		cached.handle = hTexture;
		cached.record = m_Textures[hEntry];
		return cached.record;
	}

	struct RetiredTextureViewsDX12
	{
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv, dsv;
		uint64_t fence = 0;
	};

	CUtlVector<RetiredTextureViewsDX12> m_RetiredTextureViews;
	ShaderAPITextureHandle_t m_hNextTexture = 16;
	uint64 m_nNextTextureAllocationSerial = 1;
	ShaderAPITextureHandle_t m_hModifiedTexture = 0;
	ShaderAPITextureHandle_t m_BoundTextures[16]{};
	ShaderAPITextureHandle_t m_VertexTextures[4]{};
	// Canonical states are immutable and block addresses survive growth. Only ClearSnapshots
	// (after the material system invalidates its handles) may discard states and their index.
	CUtlBlockVector<Snapshot> m_Snapshots;
	struct SnapshotKey
	{
		uint64_t fingerprint;
		const Snapshot *state;
	};
	struct SnapshotHash
	{
		unsigned operator()( const SnapshotKey &key ) const { return Mix64HashFunctor()( key.fingerprint ); }
	};
	struct SnapshotEqual
	{
		bool operator()( const SnapshotKey &a, const SnapshotKey &b ) const
		{
			return a.fingerprint == b.fingerprint && a.state->Matches( *b.state );
		}
	};
	CUtlHashtable<SnapshotKey, StateSnapshot_t, SnapshotHash, SnapshotEqual> m_SnapshotIndex;
	Snapshot m_ShadowState{};
	ShaderRasterState_t m_RasterState{};
	bool m_bRasterOverride = false;
	CUtlHashtable<CUtlString, ShaderVcsFile *, StringHashFunctor, StringEqualFunctor, const char *> m_NamedShaderFiles;
	struct NamedShaderRecordDX12
	{
		NamedShaderKey key;
		ShaderRecordDX12 *record;
		unsigned references = 0;
		NamedShaderRecordDX12( NamedShaderKeyView view, ShaderRecordDX12 *value ) : key( view ), record( value ) {}
	};
	struct NamedShaderComboDX12
	{
		NamedShaderRecordDX12 *owner = nullptr;
		ComboFoldProjectionDX12 projection{};
		uint64_t version = 0;
		bool folded = false;
		ShaderRecordDX12 *Record() const { return owner ? owner->record : nullptr; }
	};
	// Original keys own payloads; projected keys own the shared native records.
	CUtlHashtable<NamedShaderKey, NamedShaderComboDX12 *, NamedShaderHash, NamedShaderEqual, NamedShaderKeyView> m_NamedShaderCombos;
	CUtlHashtable<NamedShaderKey, NamedShaderRecordDX12 *, NamedShaderHash, NamedShaderEqual, NamedShaderKeyView> m_NamedShaderRecords;
	NamedShaderComboDX12 *m_BoundNamedCombos[2]{};
	bool m_bHighresNamedRoute = false;
	bool m_bEarlyDepthNamedRoute = false;
	uint64_t m_nComboFoldVersion = 0;

	struct FixedShaderKey
	{
		StateSnapshot_t snapshot;
		VertexFormat_t format;
		bool pixel;
		uint32_t textureTypes;
		uint32_t highresSamplerMask;
		uint64_t linkage;
		bool earlyDepth;

		bool operator<( const FixedShaderKey &other ) const
		{
			if ( snapshot != other.snapshot )
				return snapshot < other.snapshot;
			if ( format != other.format )
				return format < other.format;
			if ( pixel != other.pixel )
				return pixel < other.pixel;
			if ( textureTypes != other.textureTypes )
				return textureTypes < other.textureTypes;
			if ( highresSamplerMask != other.highresSamplerMask )
				return highresSamplerMask < other.highresSamplerMask;
			if ( earlyDepth != other.earlyDepth )
				return earlyDepth < other.earlyDepth;
			return linkage < other.linkage;
		}
	};

	CUtlMap<FixedShaderKey, ShaderRecordDX12 *, uint32_t> m_FixedShaders{ DefLessFunc( FixedShaderKey ) };
	bool m_TextureTransformEnabled[8]{};
	int m_TextureTransformDimension[8]{};
	bool m_TextureTransformProjected[8]{};
	float m_BumpMatrices[8][4]{};
	// Bit per texture matrix known to be identity; any other matrix write clears it (see MatrixChanged).
	uint32_t m_nTextureMatrixIdentityMask = 0;
	Vector m_AmbientLight{ 0.f, 0.f, 0.f };
	ShaderShadeMode_t m_ShadeMode = SHADER_SMOOTH;
	bool m_bForceDepthEquals = false, m_bOverrideDepthEnable = false, m_bOverrideDepthValue = true;
	MaterialCullMode_t m_CullMode = MATERIAL_CULLMODE_CCW;
	Snapshot m_ActiveSnapshot{};
	StateSnapshot_t m_hActiveSnapshotId = static_cast<StateSnapshot_t>( -1 );
	bool m_bLinearColorSpaceFramebuffer = false;

	bool EffectiveSRGBWrite() const { return m_ActiveSnapshot.srgbWrite && !m_bLinearColorSpaceFramebuffer; }

	ShaderAPITextureHandle_t m_hRenderTarget = SHADER_RENDERTARGET_BACKBUFFER;
	ShaderAPITextureHandle_t m_RenderTargets[RenderTargetBindingDX12::kMaxColorTargets] = { SHADER_RENDERTARGET_BACKBUFFER, SHADER_RENDERTARGET_NONE, SHADER_RENDERTARGET_NONE, SHADER_RENDERTARGET_NONE };
	ShaderAPITextureHandle_t m_hDepthTarget = SHADER_RENDERTARGET_DEPTHBUFFER;
	float m_ClearColor[4] = { 0, 0, 0, 1 };
	int m_nAnisotropy = 1;
	bool m_bDebugList = false, m_bDebugAll = false, m_bDebugRender = false;
	uint64_t m_nDebugListFrame = 0;
	KeyValues *m_pDebugTextureEntries = nullptr;
	IShaderUtil *m_pShaderUtil = nullptr;
	CSelectionStateDX12 m_Selection;
	int m_nStencilRef = 0;
	uint8_t m_nStencilReadMask = 0xff, m_nStencilWriteMask = 0xff;
	StencilComparisonFunction_t m_StencilCompare = STENCILCOMPARISONFUNCTION_ALWAYS;
	StencilOperation_t m_StencilPassOp = STENCILOPERATION_KEEP, m_StencilFailOp = STENCILOPERATION_KEEP, m_StencilDepthFailOp = STENCILOPERATION_KEEP;
	float m_flFogStart = 0, m_flFogEnd = 1, m_flFogZ = 0, m_flFogMaxDensity = 1;
	ConVar *m_pPixelFogConVar = nullptr;
	int m_nPixelFogRegister = -1;
	bool m_bFogDirty = true, m_bLastPixelFog = false, m_bLastFogSRGBWrite = false;
	int m_nLastFogHDR = -1;
	float m_RasterFogColor[4]{};
	float m_flHeightClipZ = 0;
	MaterialHeightClipMode_t m_HeightClipMode = MATERIAL_HEIGHTCLIPMODE_DISABLE;
	VMatrix m_UserClipView{};
	bool m_bUserClipViewOverride = false;
	bool m_bFogRadial = false, m_bDisallowAccess = false, m_bMutexEnabled = true;
	float m_WorldClipPlanes[6][4]{};
	VMatrix m_CachedWorldToClip{}, m_CachedClipToWorld{};
	bool m_bClipInverseValid = false;
	float m_FastClipPlane[4]{};
	uint32_t m_nClipPlaneMask = 0;
	bool m_bFastClipEnabled = false;
	bool m_bStencilEnabled = false, m_bAlphaToCoverage = false;
	bool m_bColorWriteOverride = false, m_bColorWriteOverrideValue = true;
	bool m_bAlphaWriteOverride = false, m_bAlphaWriteOverrideValue = true;
	CThreadFastMutex m_ShaderMutex;
	float m_FastFloatParams[64]{};
	int m_FastIntParams[64]{};
	VertexShaderHandle_t m_hBoundVS = VERTEX_SHADER_HANDLE_INVALID;
	GeometryShaderHandle_t m_hBoundGS = GEOMETRY_SHADER_HANDLE_INVALID;
	PixelShaderHandle_t m_hBoundPS = PIXEL_SHADER_HANDLE_INVALID;
	VertexBindingDX12 m_BoundVertexBuffers[16];
	VertexBindingDX12 m_DrawMeshBindings[16];
	CIndexBufferDX12 *m_pBoundIndexBuffer = nullptr;
	size_t m_nBoundIndexOffset = 0;
	IMaterial *m_pBoundMaterial = nullptr;
	// Comparison-only page identity, captured while the incoming borrowed material is live.
	IMaterial *m_pBoundMaterialPage = nullptr;
	CMeshDX12 *m_pRenderMesh = nullptr;
	int m_nRenderFirstIndex = 0, m_nRenderIndexCount = 0;
	CUtlVector<CMeshDX12 *> m_DynamicMeshes;
	// Stream-2 flex deltas (studiorender writes position, wrinkle and normal deltas); never handed out as a dynamic mesh.
	CMeshDX12 *m_pFlexMesh = nullptr;
	uint64_t m_nFrameCounter = 0;
	// Motion-vector pass (motion_vectors_dx12.cpp); contracts C2/C4/C7.
	MotionPassStateDX12 m_MotionPassState = MotionPassStateDX12::None;
	int m_nMotionPassSlot = 0;
	bool m_bMotionUnavailable = false;
	uint64_t m_nMotionMainFrame = ~0ull;
	uint8_t m_nMotionWarned = 0;
	ShaderAPITextureHandle_t m_hMotionResolveTarget = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pMotionTarget;
	D3D12_RESOURCE_STATES m_MotionTargetState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pMotionRtvHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE m_MotionRtv{};
	UINT m_nMotionTargetWidth = 0, m_nMotionTargetHeight = 0, m_nMotionTargetSamples = 0, m_nMotionTargetQuality = 0;
	ShaderRecordDX12 *m_MotionVS[2]{};
	ShaderRecordDX12 *m_pMotionPS = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pMotionReprojectRoot;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pMotionReprojectPso;
	UINT m_nMotionReprojectSamples = 0;
	float m_MotionCurViewProj[2][16]{}, m_MotionPrevViewProj[2][16]{};
	// Main-pass view/projection (VMatrix layout) captured at DX12_MOTION_PASS_BEGIN_MAIN for the frame generator's camera.
	float m_MotionMainView[16]{}, m_MotionMainProj[16]{};
	bool m_MotionCurViewProjValid[2]{}, m_MotionPrevViewProjValid[2]{};
	dx12native::DX12MotionVS m_MotionBlock{};
	uint64_t m_nMotionBlockVersion = 0;
	int m_nMotionObjectKey = 0, m_nMotionLastObjectKey = INT_MIN;
	uint32_t m_nMotionObjectOrdinal = 0;
	MotionHistoryTableDX12 m_MotionHistory[2];
	int m_nMotionHistoryCurrent = 0;
	uint32_t m_nMotionPassDraws = 0, m_nMotionPassObjects = 0, m_nMotionSuppressedPasses = 0;
	uint64_t m_nMotionLogFrame = 0;
	// PBR G-buffer targets (target layout: gbuffer_dx12.cpp). DrawBuffers appends them to PBR draws while m_bGBufferPass is set;
	// compute reads them in kGBufferReadState.
	static constexpr DXGI_FORMAT kGBufferFormats[2] = { DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R8G8B8A8_UNORM };
	static constexpr D3D12_RESOURCE_STATES kGBufferReadState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pGBufferNormal, m_pGBufferSpec;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pGBufferRtvHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE m_GBufferRtv[2]{};
	D3D12_RESOURCE_STATES m_GBufferState[2] = { D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET };
	UINT m_nGBufferWidth = 0, m_nGBufferHeight = 0, m_nGBufferSamples = 0, m_nGBufferQuality = 0;
	bool m_bGBufferPass = false, m_bGBufferWarned = false;
	// Native-AA upscaler. Every field is owned by the recording thread; replay results arrive through m_UpscalerReplay.
	static constexpr uint64_t kUpscalerReplaySlots = 4;
	CUpscalerDX12 m_Upscaler;
	bool m_bUpscalerInitialized = false;
	int m_nUpscalerMode = 0, m_nUpscalerSelectedMode = 0;
	uint64_t m_nUpscalerEnabledFrame = ~0ull;
	UpscalerKindDX12 m_UpscalerKind = UpscalerKindDX12::None;
	bool m_bUpscalerViewEligible = false, m_bUpscalerHistoryGap = true;
	uint64_t m_nUpscalerFrameToken = ~0ull;
	uint64_t m_nUpscalerQueuedFrame = ~0ull, m_nUpscalerLastDispatchFrame = ~0ull, m_nUpscalerLastSuccessFrame = ~0ull;
	uint64_t m_nUpscalerPendingSerial = 0, m_nUpscalerConsumedSerial = 0, m_nUpscalerJitterIndex = 0;
	float m_UpscalerJitter[2]{};
	double m_flUpscalerLastDispatchTime = 0.0;
	UpscalerReplayResultDX12 m_UpscalerReplay[kUpscalerReplaySlots]{};
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pUpscalerOutput;
	D3D12_RESOURCE_STATES m_UpscalerOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pComputeRoot;
	struct ComputePipelineDX12
	{
		CUtlString name;
		int staticIndex = 0, dynamicIndex = 0;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
		uint32_t constantBytes = 0;
	};
	CUtlVector<ComputePipelineDX12> m_ComputePipelines;
	CUtlVector<ShaderRecordDX12 *> m_ComputeShaderRecords;
	// DLSS-NR: the failed key suppresses per-frame re-creation until the tuning, layer count or output changes.
	bool m_bUpscalerNrHistoryGap = true, m_bUpscalerNrFailed = false, m_bUpscalerNrUnavailableLogged = false;
	DlssNrTuningDX12 m_UpscalerNrFailedTuning{};
	uint32_t m_nUpscalerNrFailedLayers = 0, m_nUpscalerNrLastReversible = 0;
	ID3D12Resource *m_pUpscalerNrFailedSource = nullptr;
	// Set only by a successful ResolveMotionTarget, never by stale marking.
	ShaderAPITextureHandle_t m_hMotionResolvedHandle = 0;
	uint64_t m_nMotionResolvedFrame = ~0ull;
	static constexpr int kMotionDepthBias = 0;
	// Frame generation. Every field is owned by the recording thread; replay results arrive through m_FrameGenReplay.
	static constexpr uint64_t kFrameGenReplaySlots = 4;
	int m_nFrameGenRequest = 0; // last (mode | multiplier << 8 | hudless) request
	FrameGenKindDX12 m_FrameGenKind = FrameGenKindDX12::None; // device kind as of this BeginFrame
	bool m_bFrameGenSelectFailed = false, m_bFrameGenWarned = false, m_bFrameGenHistoryGap = true;
	uint64_t m_nFrameGenFrameToken = ~0ull, m_nFrameGenQueuedFrame = ~0ull, m_nFrameGenFrameIdFrame = ~0ull;
	uint32_t m_nFrameGenFrameId = 0, m_nFrameGenLatchedFrameId = 0;
	int m_nFrameGenFailedWidth = 0, m_nFrameGenFailedHeight = 0;
	double m_flFrameGenLastDispatchTime = 0.0;
	uint64_t m_nFrameGenPendingSerial = 0, m_nFrameGenConsumedSerial = 0;
	FrameGenReplayResultDX12 m_FrameGenReplay[kFrameGenReplaySlots]{};
	float m_UpscalerFrameJitter[2]{}; // this frame's upscaler jitter, kept after DispatchUpscaler zeroes m_UpscalerJitter
	// IShaderAPIDX12 settings/status shared with the client thread: requests are stores, applied at BeginFrame.
	CInterlockedInt m_nFrameGenSettingsRequest; // mode | multiplier << 8 | hudless << 16
	CInterlockedInt m_nReflexRequest;
	CInterlockedInt m_nFpsLimitBits;            // float bits of the last SetFrameRateLimit
	int m_nReflexApplied = -1;
	CInterlockedInt m_nHdrOutputEnabled;
	CInterlockedInt m_nHdrOutputUiNitsBits;     // float bits of the last SetHdrOutput UI nits
	float m_flFrameGenFpsLimit = 0.f;
	CInterlockedInt m_nFrameGenStatus;
	CInterlockedInt m_nFramesShown;
	CInterlockedInt m_nReflexStatus;
	char m_szFrameGenError[256] = {};
	uint32_t m_nFrameDrawCount = 0;
	uint32_t m_nFrameFlushCount = 0;
	uint32_t m_nFrameSyncCount = 0;
	bool m_bFrameActive = false;
	OcclusionQueryDX12 *CreateOcclusionQuery();
	// Hash handles are only hints: validate occupancy and the complete key after any table mutation.
	UtlHashHandle_t m_NamedReferenceHints[2] = { decltype( m_NamedShaderReferences )::InvalidHandle(), decltype( m_NamedShaderReferences )::InvalidHandle() };
	UtlHashHandle_t m_NamedComboHints[2] = { decltype( m_NamedShaderCombos )::InvalidHandle(), decltype( m_NamedShaderCombos )::InvalidHandle() };

	// Per-snapshot named combo results. Bumping the epoch drops every entry; required whenever
	// combos are purged, snapshots are cleared or reference marks must be re-established.
	struct NamedResolveEntryDX12
	{
		StateSnapshot_t snapshot = static_cast<StateSnapshot_t>( -1 );
		int dynamicIndex = 0;
		uint64_t epoch = 0;
		ShaderRecordDX12 *record = nullptr;
		NamedShaderComboDX12 *combo = nullptr;
		UtlHashHandle_t reference = 0;
		uint64_t referenceEpoch = 0;
	};

	// Bumped when reference marks are cleared; cached entries re-mark their reference before reuse.
	uint64_t m_nNamedReferenceEpoch = 1;
	NamedResolveEntryDX12 m_NamedResolveCache[2][1024];
	uint64_t m_nNamedResolveEpoch = 1;
	ShaderRecordDX12 *ResolveActiveNamedShader( bool bPixel, int nDynamicIndex );
	// Exact-input transform cache: recomputed only when view/projection bytes differ.
	VMatrix m_CachedTransformView{}, m_CachedTransformProjection{}, m_CachedViewProjection{};
	Vector m_CachedCameraPosition{ 0.f, 0.f, 0.f };
	bool m_bCachedTransformValid = false, m_bCachedCameraValid = false;

	// Exact-input fog cache. Zero-initialized before filling so memcmp covers padding.
	struct FogInputsDX12
	{
		float start, end, z, density, camera[3], tone;
		int mode, passFog, hdr;
		unsigned char color[3];
		bool pixelFog, srgbWrite, gammaDisabled;
	};

	FogInputsDX12 m_LastFogInputs{};
	float m_FogVertexParams[4]{}, m_FogCameraParams[4]{}, m_FogPixelParams[4]{}, m_FogPixelColor[4]{};
	bool m_bFogOutputsValid = false;
	float m_flToneScaleGamma = 1.f;
	bool m_bToneScaleConstantValid = false;

	// Render-target descriptions. The reference pins the resource, so pointer equality cannot alias a new object.
	struct CachedResourceDescDX12
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		D3D12_RESOURCE_DESC desc{};

		const D3D12_RESOURCE_DESC &Get( ID3D12Resource *pResource )
		{
			if ( resource.Get() != pResource )
			{
				resource = pResource;
				desc = pResource->GetDesc();
			}
			return desc;
		}
	};

	CachedResourceDescDX12 m_TargetDescs[RenderTargetBindingDX12::kMaxColorTargets + 1];
	// Recorded blits name their RTV until replay, so blits rotate through a ring (see BlitTexture).
	static constexpr int kBlitRtvSlots = 64;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pBlitRtvHeap;
	int m_nBlitRtvSlot = 0;
};

extern CShaderAPIDX12 *g_pShaderAPIDX12;
} // namespace shaderapidx12

#endif // SHADERAPI_DX12_H
