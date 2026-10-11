//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Realtime deferred light editor (selection, gizmos, free-fly view).
//
//===========================================================================//

#ifndef DEFERRED_LIGHT_EDITOR_H
#define DEFERRED_LIGHT_EDITOR_H

#ifdef _WIN32
#pragma once
#endif

#include "cbase.h"
#include "deferred/deferred_lights.h"

class CUserCmd;

enum EditorLightField_t
{
	EDITORLIGHT_ORIGIN = 0,
	EDITORLIGHT_ANGLES,
	EDITORLIGHT_COLOR,
	EDITORLIGHT_INTENSITY,
	EDITORLIGHT_RADIUS,
	EDITORLIGHT_INNERCONE,
	EDITORLIGHT_OUTERCONE,
	EDITORLIGHT_SPOT,
	EDITORLIGHT_SHADOW,
	EDITORLIGHT_COOKIE,
};

class CLightingEditor : public CAutoGameSystemPerFrame
{
	typedef CAutoGameSystemPerFrame BaseClass;
public:
	CLightingEditor();

	bool Init();
	void LevelShutdownPreEntity();
	void Update( float ft );
	void PostRender();

	void SetEditorActive( bool bActive );
	bool IsEditorActive();

	// Latches the player view the editor opened on and swallows the usercmd.
	// ClientModeShared::CreateMove calls this before the player sees the cmd.
	bool SuppressPlayerInput( CUserCmd *cmd );

	void GetEditorView( Vector &origin, QAngle &angles );
	void SetEditorView( const Vector &origin, const QAngle &angles );

	Vector &EditorMoveDir();
	void AbortEditorMovement( bool bStompVelocity );

	enum EDITORINTERACTION_MODE
	{
		EDITORINTERACTION_SELECT = 0,
		EDITORINTERACTION_ADD,
		EDITORINTERACTION_TRANSLATE,
		EDITORINTERACTION_ROTATE,

		EDITORINTERACTION_COUNT,
	};

	void SetEditorInteractionMode( EDITORINTERACTION_MODE mode );
	EDITORINTERACTION_MODE GetEditorInteractionMode();

	enum EDITOR_SELECTEDAXIS
	{
		EDITORAXIS_NONE = 0,
		EDITORAXIS_X,
		EDITORAXIS_Y,
		EDITORAXIS_Z,
		EDITORAXIS_SCREEN,
		EDITORAXIS_PLANE_XY,
		EDITORAXIS_PLANE_XZ,
		EDITORAXIS_PLANE_YZ,

		EDITORAXIS_COUNT,
		EDITORAXIS_FIRST = EDITORAXIS_X,
		EDITORAXIS_FIRST_PLANE = EDITORAXIS_PLANE_XY,
	};

	EDITOR_SELECTEDAXIS GetCurrentSelectedAxis();
	void SetCurrentSelectedAxis( EDITOR_SELECTEDAXIS axis );
	void UpdateCurrentSelectedAxis( int x, int y );

	void AddLightFromScreen( int x, int y );
	bool SelectLight( int x, int y, bool bGroup, bool bToggle );
	bool SelectLights( int *pi2_BoundsMin, int *pi2_BoundsMax, bool bGroup, bool bToggle );
	void MoveSelectedLights( Vector delta );
	void RotateSelectedLights( VMatrix matRotate );

	void ClearSelection();
	bool IsLightSelected( int id );
	void DeleteSelectedLights();
	DeferredLight_t *FirstSelected();
	void SyncTemplateFromSelection();
	const DeferredLight_t &AddTemplate();
	void CommitField( EditorLightField_t field, const DeferredLight_t &value );

	int GetNumSelected();
	bool IsAnythingSelected();
	Vector GetSelectionCenter();
	QAngle GetSelectionOrientation();
	void SetSelectionCenterLocked( bool locked );

	Vector GetViewOrigin();
	QAngle GetViewAngles();
	Vector GetPickingRay( int x, int y );

private:
	void EnsureMaterials();
	void RenderSprites();
	void RenderSelection();
	void RenderHelpers();
	void RenderTranslate();
	void RenderRotate();
	void PurgeMissingSelection();


	bool m_bActive;
	bool m_bSelectionCenterLocked;
	Vector m_vecSelectionCenterCache;

	CUtlVector< int > m_Selected;
	DeferredLight_t m_AddTemplate;

	Vector m_vecEditorView_Origin;
	QAngle m_angEditorView_Angles;
	QAngle m_angPlayerView;
	Vector m_vecMoveDir;
	Vector m_vecEditorView_Velocity;

	EDITORINTERACTION_MODE m_iInteractionMode;
	EDITOR_SELECTEDAXIS m_iCurrentSelectedAxis;

	CMaterialReference m_matSprite_Light;
	CMaterialReference m_matSelect;
	CMaterialReference m_matHelper;
	bool m_bMaterialsReady;
};

CLightingEditor *GetLightingEditor();

#endif
