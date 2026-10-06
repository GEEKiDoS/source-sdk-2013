//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef LIGHTING_DX12_H
#define LIGHTING_DX12_H
#pragma once

#include "shaderapi/ishaderapidx12lighting.h"
#include "pipeline_dx12.h"
#include "textures_dx12.h"
#include "tier1/utlstring.h"

namespace shaderapidx12
{
class CShaderAPIDX12;
class CShaderDeviceDX12;
struct LightingPacketDX12;
struct ShaderRecordDX12;
bool ValidateLightingShaderDX12( const D3D12_SHADER_BYTECODE &bytecode, bool pixelStage, bool *lightingAbi, CUtlString &error, bool *sunVisibility = nullptr );
bool ValidateShadowDepthRestoreShaderDX12( const D3D12_SHADER_BYTECODE &bytecode, bool pixelStage );

struct SunReceiverStreamDX12
{
	const CVertexBufferDX12 *buffer = nullptr;
	uint32 byteOffset = 0, firstVertex = 0, vertexCount = 0, repetitions = 1;
};
struct SunReceiverDrawDX12
{
	SunReceiverStreamDX12 streams[16];
	const CIndexBufferDX12 *indices = nullptr;
	const VertexLayoutDX12 *layout = nullptr;
	const ShaderRecordDX12 *pixelShader = nullptr;
	const char *vertexLogical = nullptr, *pixelLogical = nullptr;
	size_t indexOffset = 0;
	MaterialPrimitiveType_t primitive = MATERIAL_TRIANGLES;
	int firstIndex = 0, indexCount = 0;
};
struct SunReceiverCoordinatesDX12
{
	// Float2 values indexed by the original vertex ID. Borrowed through this Draw,
	// including submission/reclaim; the next resolve or control mutation may retire them.
	const float *values = nullptr;
	uint32 count = 0;
};
// Device-owned control plane; mutable GPU recording state belongs exclusively to the recording owner.
class CLightingDX12 final : public IShaderAPIDX12Lighting
{
public:
	CLightingDX12();
	~CLightingDX12();
	void Initialize( CShaderDeviceDX12 *device, CShaderAPIDX12 *api );
	void Shutdown();
	bool ValidateMap( const DX12LightingMapDesc &map, char *error, int errorBytes ) override;
	void PrepareMap( const DX12LightingMapDesc &map ) override;
	DX12LightingStatus GetStatus( uint32 mapGeneration, uint32 viewGeneration, char *error, int errorBytes ) override;
	void BeginView( const DX12LightingViewPacket &view ) override;
	DX12ShadowTarget_t CreateShadowDepthTarget( const char *name, int width, int height ) override;
	IRefCounted *RetainShadowDepthTarget( DX12ShadowTarget_t target ) override;
	void DestroyShadowDepthTarget( DX12ShadowTarget_t target ) override;
	void BeginShadowPass( DX12ShadowTarget_t target, int x, int y, int width, int height, bool clear ) override;
	void EndShadowPass() override;
	void CopyShadowDepthRect( DX12ShadowTarget_t dst, DX12ShadowTarget_t src, int dstX, int dstY, int srcX, int srcY, int width, int height ) override;
	void EndView() override;
	void UnloadMap( uint32 mapGeneration ) override;
	void SetReceiverFeatureGeneration( uint32 mapGeneration ) override;
	uint32 ReceiverFeatureGeneration() override;
	void RejectUnsupportedLitShader( const char *shaderName ) override;
	void GetSunVisibilityStats( uint32 mapGeneration, DX12LightingSunVisibilityStats &stats ) override;
	bool ResolveSunReceiverDraw( const SunReceiverDrawDX12 &draw, SunReceiverCoordinatesDX12 &coordinates );
	void ForgetSunReceiverTexture( ShaderAPITextureHandle_t texture );

	bool ShadowPassActive() const;
	bool PrepareShadowDraw( RenderTargetBindingDX12 &target, D3D12_VIEWPORT &viewport, D3D12_RECT &scissor );
	bool PrepareReceiverDraw( bool lightingAbi, CPipelineCacheDX12::BindingInputDX12 &input );
	bool PresentationBlocked();
	void FailRecording( const char *error );
	void Reclaim();
	void Execute( LightingPacketDX12 *packet );
	void Recycle( LightingPacketDX12 *packet );
private:
	LightingPacketDX12 *Packet( int operation );
	void Enqueue( LightingPacketDX12 *packet );
	struct Impl;
	Impl *m_Impl;
};
} // namespace shaderapidx12
#endif // LIGHTING_DX12_H
