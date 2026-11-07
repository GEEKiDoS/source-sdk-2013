//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_GLOW_SCENE_H
#define TERROR_GLOW_SCENE_H
#ifdef _WIN32
#pragma once
#endif

class CViewSetup;
class C_TerrorPlayer;
struct ShaderStencilState_t;

// The actor owns its already-flattened selection/interval/frame fields.
// Only the real user0 view/frame producer calls this native scene graph.
void TerrorGlow_UpdateView( const CViewSetup &view, int slot );
void TerrorGlow_DrawStencil( const CViewSetup &view, int slot );
bool TerrorGlow_DrawColor( int slot, bool draw );
float TerrorGlow_GetCompositeScale( int slot );
bool TerrorGlow_ShouldRender( int slot );
bool TerrorGlow_ApplyPlayerStencil( C_TerrorPlayer *player );
bool TerrorGlow_GetParentStencilState( C_TerrorPlayer *player, ShaderStencilState_t &state ); // C2FFF0.

#endif // TERROR_GLOW_SCENE_H
