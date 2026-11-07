//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Realtime deferred light editor (selection, gizmos, free-fly view).
//
//===========================================================================//

#include "cbase.h"
#include "deferred/deferred_light_editor.h"
#include "view.h"
#include "viewrender.h"
#include "beamdraw.h"
#include "cdll_util.h"
#include "collisionutils.h"
#include "usercmd.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern void ScreenToWorld( int mousex, int mousey, float fov,
					const Vector& vecRenderOrigin,
					const QAngle& vecRenderAngles,
					Vector& vecPickingRay );

extern ConVar building_cubemaps;
#define LIGHT_BOX_SIZE 8.0f
#define LIGHT_BOX_VEC Vector( LIGHT_BOX_SIZE, LIGHT_BOX_SIZE, LIGHT_BOX_SIZE )
#define HELPER_COLOR_MAX 0.8f
#define HELPER_COLOR_MIN 0.1f
#define HELPER_COLOR_LOW 0.4f
#define SELECTED_AXIS_COLOR Vector( HELPER_COLOR_MAX, HELPER_COLOR_MAX, HELPER_COLOR_MIN )


static CLightingEditor g_LightingEditor;

CLightingEditor *GetLightingEditor()
{
	return &g_LightingEditor;
}

static KeyValues *MakeEditorVmt( bool bAdditive )
{
	KeyValues *pVmt = new KeyValues( "UnlitGeneric" );
	pVmt->SetString( "$basetexture", "vgui/white" );
	pVmt->SetInt( "$vertexcolor", 1 );
	pVmt->SetInt( "$vertexalpha", 1 );
	pVmt->SetInt( "$nocull", 1 );
	pVmt->SetInt( "$ignorez", 1 );
	pVmt->SetInt( "$nofog", 1 );
	if ( bAdditive )
		pVmt->SetInt( "$additive", 1 );
	return pVmt;
}

static void ApplyLightField( DeferredLight_t *pLight, EditorLightField_t field, const DeferredLight_t &value )
{
	switch ( field )
	{
	case EDITORLIGHT_ORIGIN:
		pLight->origin = value.origin;
		break;
	case EDITORLIGHT_ANGLES:
		pLight->angles = value.angles;
		break;
	case EDITORLIGHT_COLOR:
		pLight->color = value.color;
		break;
	case EDITORLIGHT_INTENSITY:
		pLight->intensity = value.intensity;
		break;
	case EDITORLIGHT_RADIUS:
		pLight->radius = value.radius;
		break;
	case EDITORLIGHT_INNERCONE:
		pLight->innerCone = value.innerCone;
		break;
	case EDITORLIGHT_OUTERCONE:
		pLight->outerCone = value.outerCone;
		break;
	case EDITORLIGHT_SPOT:
		pLight->spot = value.spot;
		break;
	case EDITORLIGHT_SHADOW:
		pLight->shadow = value.shadow;
		break;
	case EDITORLIGHT_VOLUMETRIC:
		pLight->volumetric = value.volumetric;
		break;
	case EDITORLIGHT_COOKIE:
		Q_strncpy( pLight->cookie, value.cookie, sizeof( pLight->cookie ) );
		break;
	}
}

CLightingEditor::CLightingEditor()
	: BaseClass( "LightingEditorSystem" )
{
	m_bActive = false;
	m_bSelectionCenterLocked = false;
	m_bMaterialsReady = false;
	m_vecSelectionCenterCache.Init();
	m_vecEditorView_Origin = vec3_origin;
	m_angEditorView_Angles = vec3_angle;
	m_angPlayerView = vec3_angle;
	m_vecMoveDir = vec3_origin;
	m_vecEditorView_Velocity = vec3_origin;
	m_iInteractionMode = EDITORINTERACTION_SELECT;
	m_iCurrentSelectedAxis = EDITORAXIS_NONE;

	m_AddTemplate.origin = vec3_origin;
	m_AddTemplate.angles = vec3_angle;
	m_AddTemplate.color.Init( 1.0f, 1.0f, 1.0f );
	m_AddTemplate.intensity = 1.0f;
	m_AddTemplate.radius = 256.0f;
	m_AddTemplate.innerCone = 35.0f;
	m_AddTemplate.outerCone = 45.0f;
	m_AddTemplate.spot = false;
	m_AddTemplate.shadow = true;
	m_AddTemplate.volumetric = false;
	m_AddTemplate.cookie[0] = 0;
	m_AddTemplate.id = 0;
}

bool CLightingEditor::Init()
{
	return true;
}

void CLightingEditor::LevelShutdownPreEntity()
{
	SetEditorActive( false );
	ClearSelection();
	m_iCurrentSelectedAxis = EDITORAXIS_NONE;
}

void CLightingEditor::EnsureMaterials()
{
	if ( m_bMaterialsReady )
		return;

	m_matSprite_Light.Init( "sprites/light_glow02_add_noz", TEXTURE_GROUP_OTHER );
	m_matSelect.Init( "deferred_editor_select", MakeEditorVmt( false ) );
	m_matHelper.Init( "deferred_editor_helper", MakeEditorVmt( false ) );
	m_bMaterialsReady = true;
}

void CLightingEditor::Update( float ft )
{
	if ( !m_bActive )
		return;

	// CreateMove already latches these; reinforce so mouse-look cannot accumulate
	// if a frame samples angles outside that hook.
	engine->SetViewAngles( m_angPlayerView );

	Vector fwd, right;
	AngleVectors( m_angEditorView_Angles, &fwd, &right, NULL );

	float flIdealSpeed = 300.0f + m_vecMoveDir.z * 400.0f;
	Vector vecIdealMove = ( fwd * m_vecMoveDir.x + right * m_vecMoveDir.y ) * flIdealSpeed;
	Vector vecDelta = vecIdealMove - m_vecEditorView_Velocity;
	if ( !vecDelta.IsZero() )
		m_vecEditorView_Velocity += vecDelta * ft * 10.0f;

	if ( m_vecEditorView_Velocity.LengthSqr() > 1.0f )
		m_vecEditorView_Origin += m_vecEditorView_Velocity * ft;
}

void CLightingEditor::SetEditorActive( bool bActive )
{
	if ( m_bActive == bActive )
		return;

	m_bActive = bActive;
	AbortEditorMovement( true );
	if ( bActive )
		engine->GetViewAngles( m_angPlayerView );
}

bool CLightingEditor::IsEditorActive()
{
	return m_bActive;
}

bool CLightingEditor::SuppressPlayerInput( CUserCmd *cmd )
{
	if ( !m_bActive )
		return false;

	cmd->forwardmove = 0;
	cmd->sidemove = 0;
	cmd->upmove = 0;
	cmd->buttons = 0;
	cmd->impulse = 0;
	cmd->weaponselect = 0;
	cmd->weaponsubtype = 0;
	cmd->mousedx = 0;
	cmd->mousedy = 0;
	cmd->viewangles = m_angPlayerView;
	engine->SetViewAngles( m_angPlayerView );
	return true;
}

void CLightingEditor::GetEditorView( Vector &origin, QAngle &angles )
{
	origin = m_vecEditorView_Origin;
	angles = m_angEditorView_Angles;
}

void CLightingEditor::SetEditorView( const Vector &origin, const QAngle &angles )
{
	m_vecEditorView_Origin = origin;
	m_angEditorView_Angles = angles;
}

Vector &CLightingEditor::EditorMoveDir()
{
	return m_vecMoveDir;
}

void CLightingEditor::AbortEditorMovement( bool bStompVelocity )
{
	m_vecMoveDir.Init();
	if ( bStompVelocity )
		m_vecEditorView_Velocity.Init();
}

void CLightingEditor::SetEditorInteractionMode( EDITORINTERACTION_MODE mode )
{
	m_iInteractionMode = mode;
}

CLightingEditor::EDITORINTERACTION_MODE CLightingEditor::GetEditorInteractionMode()
{
	return m_iInteractionMode;
}

CLightingEditor::EDITOR_SELECTEDAXIS CLightingEditor::GetCurrentSelectedAxis()
{
	return m_iCurrentSelectedAxis;
}

void CLightingEditor::SetCurrentSelectedAxis( EDITOR_SELECTEDAXIS axis )
{
	m_iCurrentSelectedAxis = axis;
}

Vector CLightingEditor::GetViewOrigin()
{
	return view->GetViewSetup()->origin;
}

QAngle CLightingEditor::GetViewAngles()
{
	return view->GetViewSetup()->angles;
}

Vector CLightingEditor::GetPickingRay( int x, int y )
{
	const CViewSetup *pSetup = view->GetViewSetup();
	float flRatio = ( 4.0f / 3.0f ) / ( pSetup->width / (float)pSetup->height );
	float flFov = ScaleFOVByWidthRatio( pSetup->fov, flRatio );
	Vector ray;
	ScreenToWorld( x, y, flFov, pSetup->origin, pSetup->angles, ray );
	return ray;
}

void CLightingEditor::PurgeMissingSelection()
{
	for ( int i = m_Selected.Count() - 1; i >= 0; --i )
	{
		if ( !DeferredLights().Find( m_Selected[i] ) )
			m_Selected.Remove( i );
	}
}

DeferredLight_t *CLightingEditor::FirstSelected()
{
	PurgeMissingSelection();
	if ( !m_Selected.Count() )
		return NULL;
	return DeferredLights().Find( m_Selected[0] );
}

void CLightingEditor::PostRender()
{
	if ( !m_bActive || building_cubemaps.GetBool() )
		return;

	// The 3D skybox calls PostRender too, and leaves CurrentViewID on VIEW_3DSKY
	// after the main world view restores that id. Draw only while the active view
	// matrix is the main camera.
	CMatRenderContextPtr pRenderContext( materials );
	VMatrix viewMatrix, viewToWorld;
	pRenderContext->GetMatrix( MATERIAL_VIEW, &viewMatrix );
	if ( !MatrixInverseGeneral( viewMatrix, viewToWorld ) )
		return;
	const CViewSetup *pSetup = view->GetViewSetup();
	if ( ( viewToWorld.GetTranslation() - pSetup->origin ).LengthSqr() > ( 32.0f * 32.0f ) )
		return;

	EnsureMaterials();

	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();
	MaterialFogMode_t fog = pRenderContext->GetFogMode();
	pRenderContext->FogMode( MATERIAL_FOG_NONE );

	RenderSprites();
	RenderSelection();
	RenderHelpers();

	pRenderContext->FogMode( fog );
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PopMatrix();
}

struct SortedEditorLight_t
{
	float distSqr;
	int index;
};

static int SortEditorLightsFarFirst( const SortedEditorLight_t *a, const SortedEditorLight_t *b )
{
	return ( a->distSqr > b->distSqr ) ? -1 : 1;
}

void CLightingEditor::RenderSprites()
{
	const CUtlVector< DeferredLight_t > &lights = DeferredLights().Lights();
	if ( !lights.Count() )
		return;

	Vector viewOrigin = m_vecEditorView_Origin;
	CUtlVector< SortedEditorLight_t > sorted;
	sorted.SetCount( lights.Count() );
	for ( int i = 0; i < lights.Count(); i++ )
	{
		sorted[i].index = i;
		sorted[i].distSqr = ( lights[i].origin - viewOrigin ).LengthSqr();
	}
	sorted.Sort( SortEditorLightsFarFirst );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( m_matSprite_Light );
	Vector right, up;
	AngleVectors( m_angEditorView_Angles, NULL, &right, &up );

	for ( int i = 0; i < sorted.Count(); i++ )
	{
		const DeferredLight_t &light = lights[ sorted[i].index ];
		Vector color = light.color;
		float flMax = MAX( color.x, MAX( color.y, color.z ) );
		if ( flMax < 1e-3f )
			color.Init( 0.6f, 0.6f, 0.6f );
		else
			color /= flMax;

		IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, m_matSprite_Light );
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );
		float flSize = sqrtf( sorted[i].distSqr ) * 0.03f;
		if ( flSize < 16.0f )
			flSize = 16.0f;
		Vector corners[4] = {
			light.origin - right * flSize - up * flSize,
			light.origin + right * flSize - up * flSize,
			light.origin + right * flSize + up * flSize,
			light.origin - right * flSize + up * flSize,
		};
		Vector2D uvs[4] = { Vector2D( 0, 1 ), Vector2D( 1, 1 ), Vector2D( 1, 0 ), Vector2D( 0, 0 ) };
		for ( int v = 0; v < 4; v++ )
		{
			meshBuilder.Position3fv( corners[v].Base() );
			meshBuilder.Color3fv( color.Base() );
			meshBuilder.TexCoord2fv( 0, uvs[v].Base() );
			meshBuilder.AdvanceVertex();
		}
		meshBuilder.End();
		pMesh->Draw();
	}
}

void CLightingEditor::RenderSelection()
{
	PurgeMissingSelection();
	if ( !m_Selected.Count() )
		return;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( m_matSelect );
	IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, m_matSelect );
	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_QUADS, 6 * m_Selected.Count() );

	int iPlaneSetup[6][4] = {
		{ 7, 5, 3, 6 },
		{ 0, 3, 5, 2 },
		{ 1, 4, 7, 6 },
		{ 0, 1, 6, 3 },
		{ 2, 5, 7, 4 },
		{ 0, 2, 4, 1 },
	};
	Vector2D uvs[4] = {
		Vector2D( 0, 0 ),
		Vector2D( 1, 0 ),
		Vector2D( 1, 1 ),
		Vector2D( 0, 1 ),
	};
	Vector color( 0.5f, 1.0f, 0.25f );

	FOR_EACH_VEC( m_Selected, i )
	{
		DeferredLight_t *pLight = DeferredLights().Find( m_Selected[i] );
		Vector position = pLight->origin;
		float flSize = LIGHT_BOX_SIZE;
		Vector points[8] = {
			Vector( flSize, flSize, flSize ),
			Vector( -flSize, flSize, flSize ),
			Vector( flSize, -flSize, flSize ),
			Vector( flSize, flSize, -flSize ),
			Vector( -flSize, -flSize, flSize ),
			Vector( flSize, -flSize, -flSize ),
			Vector( -flSize, flSize, -flSize ),
			Vector( -flSize, -flSize, -flSize ),
		};
		for ( int p = 0; p < 8; p++ )
			points[p] += position;

		for ( int p = 0; p < 6; p++ )
		{
			for ( int v = 0; v < 4; v++ )
			{
				meshBuilder.Position3fv( points[ iPlaneSetup[p][v] ].Base() );
				meshBuilder.Color3fv( color.Base() );
				meshBuilder.TexCoord2fv( 0, uvs[v].Base() );
				meshBuilder.AdvanceVertex();
			}
		}
	}

	meshBuilder.End();
	pMesh->Draw();
}

void CLightingEditor::RenderHelpers()
{
	if ( !IsAnythingSelected() )
		return;

	switch ( GetEditorInteractionMode() )
	{
	case EDITORINTERACTION_TRANSLATE:
		RenderTranslate();
		break;
	case EDITORINTERACTION_ROTATE:
		RenderRotate();
		break;
	default:
		break;
	}
}

void CLightingEditor::RenderTranslate()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( m_matHelper );

	const bool bMultiSelected = GetNumSelected() > 1;
	const int iSubDiv = 16;
	const float flLengthBase = 16.0f + ( bMultiSelected ? LIGHT_BOX_SIZE : 0 );
	const float flLengthTip = 8.0f;
	const float flRadiusBase = 1.2f;
	const float flRadiusTip = 4.0f;
	const float flAngleStep = 360.0f / iSubDiv;
	const float flOffset = bMultiSelected ? 0 : LIGHT_BOX_SIZE;

	Vector directions[3] = {
		Vector( 1, 0, 0 ),
		Vector( 0, 1, 0 ),
		Vector( 0, 0, 1 ),
	};
	Vector colors[3] = {
		Vector( HELPER_COLOR_MAX, HELPER_COLOR_MIN, HELPER_COLOR_MIN ),
		Vector( HELPER_COLOR_MIN, HELPER_COLOR_MAX, HELPER_COLOR_MIN ),
		Vector( HELPER_COLOR_MIN, HELPER_COLOR_MIN, HELPER_COLOR_MAX ),
	};

	Vector center = GetSelectionCenter();

	for ( int iDir = 0; iDir < 3; iDir++ )
	{
		Vector fwd = directions[iDir];
		QAngle ang;
		VectorAngles( fwd, ang );

		IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, m_matHelper );
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, 5 * iSubDiv );

		Vector colHigh = colors[iDir];
		if ( GetCurrentSelectedAxis() == EDITORAXIS_FIRST + iDir )
			colHigh = SELECTED_AXIS_COLOR;
		Vector colLow = colHigh * HELPER_COLOR_LOW;

		for ( int iStep = 0; iStep < iSubDiv; iStep++ )
		{
			QAngle angNext( ang );
			angNext.z += flAngleStep;

			Vector cUp, nUp;
			AngleVectors( ang, NULL, NULL, &cUp );
			AngleVectors( angNext, NULL, NULL, &nUp );

			Vector pos = center + fwd * flOffset;
			Vector tmp0 = pos + cUp * flRadiusBase;
			Vector tmp1 = pos + nUp * flRadiusBase;

			meshBuilder.Position3fv( pos.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colLow.Base() );
			meshBuilder.AdvanceVertex();
			meshBuilder.Position3fv( tmp0.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colLow.Base() );
			meshBuilder.AdvanceVertex();
			meshBuilder.Position3fv( tmp1.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colLow.Base() );
			meshBuilder.AdvanceVertex();

			Vector tmp2 = tmp0 + fwd * flLengthBase;
			meshBuilder.Position3fv( tmp1.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colHigh.Base() );
			meshBuilder.AdvanceVertex();
			meshBuilder.Position3fv( tmp0.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colHigh.Base() );
			meshBuilder.AdvanceVertex();
			meshBuilder.Position3fv( tmp2.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colHigh.Base() );
			meshBuilder.AdvanceVertex();

			Vector tmp3 = tmp1 + fwd * flLengthBase;
			meshBuilder.Position3fv( tmp1.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colHigh.Base() );
			meshBuilder.AdvanceVertex();
			meshBuilder.Position3fv( tmp2.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colHigh.Base() );
			meshBuilder.AdvanceVertex();
			meshBuilder.Position3fv( tmp3.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colHigh.Base() );
			meshBuilder.AdvanceVertex();

			pos += fwd * flLengthBase;
			tmp0 = pos + cUp * flRadiusTip;
			tmp1 = pos + nUp * flRadiusTip;
			meshBuilder.Position3fv( pos.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colLow.Base() );
			meshBuilder.AdvanceVertex();
			meshBuilder.Position3fv( tmp0.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colLow.Base() );
			meshBuilder.AdvanceVertex();
			meshBuilder.Position3fv( tmp1.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colLow.Base() );
			meshBuilder.AdvanceVertex();

			pos += fwd * flLengthTip;
			meshBuilder.Position3fv( tmp1.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colHigh.Base() );
			meshBuilder.AdvanceVertex();
			meshBuilder.Position3fv( tmp0.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colHigh.Base() );
			meshBuilder.AdvanceVertex();
			meshBuilder.Position3fv( pos.Base() );
			meshBuilder.TexCoord2f( 0, 0, 0 );
			meshBuilder.Color3fv( colHigh.Base() );
			meshBuilder.AdvanceVertex();

			ang = angNext;
		}

		meshBuilder.End();
		pMesh->Draw();

		if ( GetCurrentSelectedAxis() == EDITORAXIS_FIRST_PLANE + iDir )
		{
			const float flPlaneSize = LIGHT_BOX_SIZE + 12.0f;
			int orient[3][2] = {
				{ 0, 1 },
				{ 2, 0 },
				{ 1, 2 },
			};
			int axis0 = orient[iDir][0];
			int axis1 = orient[iDir][1];
			Vector points[4];
			points[0] = points[1] = points[2] = points[3] = center;
			points[1][axis0] += flPlaneSize;
			points[2][axis0] += flPlaneSize;
			points[2][axis1] += flPlaneSize;
			points[3][axis1] += flPlaneSize;

			Vector col = SELECTED_AXIS_COLOR;
			meshBuilder.Begin( pMesh, MATERIAL_QUADS, 2 );
			for ( int v = 3; v >= 0; --v )
			{
				meshBuilder.Position3fv( points[v].Base() );
				meshBuilder.Color3fv( col.Base() );
				meshBuilder.TexCoord2f( 0, 0, 0 );
				meshBuilder.AdvanceVertex();
			}
			col *= HELPER_COLOR_LOW;
			for ( int v = 0; v < 4; v++ )
			{
				meshBuilder.Position3fv( points[v].Base() );
				meshBuilder.Color3fv( col.Base() );
				meshBuilder.TexCoord2f( 0, 0, 0 );
				meshBuilder.AdvanceVertex();
			}
			meshBuilder.End();
			pMesh->Draw();
		}
	}
}

void CLightingEditor::RenderRotate()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( m_matHelper );

	const int iSubDiv = 64;
	const float flRadius = 32.0f;
	const float flThickness = 4.0f;
	const float flRotationStep = 360.0f / iSubDiv;

	Vector center = GetSelectionCenter();
	QAngle orientation = GetSelectionOrientation();
	Vector colors[3] = {
		Vector( HELPER_COLOR_MAX, HELPER_COLOR_MIN, HELPER_COLOR_MIN ),
		Vector( HELPER_COLOR_MIN, HELPER_COLOR_MAX, HELPER_COLOR_MIN ),
		Vector( HELPER_COLOR_MIN, HELPER_COLOR_MIN, HELPER_COLOR_MAX ),
	};

	Vector axes[3];
	AngleVectors( orientation, &axes[0], &axes[1], &axes[2] );
	int iLookup[3][2] = {
		{ 1, 2 },
		{ 2, 1 },
		{ 0, 2 },
	};

	for ( int iDir = 0; iDir < 3; iDir++ )
	{
		Vector colHigh = colors[iDir];
		if ( iDir == GetCurrentSelectedAxis() - EDITORAXIS_FIRST )
			colHigh = SELECTED_AXIS_COLOR;
		Vector colLow = colHigh * HELPER_COLOR_LOW;

		IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, m_matHelper );
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, 2 * iSubDiv );

		QAngle angCur, angNext;
		VectorAngles( axes[ iLookup[iDir][0] ], axes[ iLookup[iDir][1] ], angCur );
		angNext = angCur;
		float flCurrentRadius = flRadius - iDir * 0.275f;

		for ( int iStep = 0; iStep < iSubDiv; iStep++ )
		{
			angNext.z += flRotationStep;
			Vector cUp, nUp, cFwd;
			AngleVectors( angCur, &cFwd, NULL, &cUp );
			AngleVectors( angNext, NULL, NULL, &nUp );

			Vector tmp[4];
			tmp[0] = center + cUp * flCurrentRadius + cFwd * flThickness;
			tmp[1] = center + nUp * flCurrentRadius + cFwd * flThickness;
			tmp[2] = tmp[1] - cFwd * flThickness * 2.0f;
			tmp[3] = tmp[0] - cFwd * flThickness * 2.0f;

			for ( int iPoint = 0; iPoint < 4; iPoint++ )
			{
				meshBuilder.Position3fv( tmp[iPoint].Base() );
				meshBuilder.Color3fv( colHigh.Base() );
				meshBuilder.TexCoord2f( 0, 0, 0 );
				meshBuilder.AdvanceVertex();
			}
			for ( int iPoint = 3; iPoint >= 0; --iPoint )
			{
				meshBuilder.Position3fv( tmp[iPoint].Base() );
				meshBuilder.Color3fv( colLow.Base() );
				meshBuilder.TexCoord2f( 0, 0, 0 );
				meshBuilder.AdvanceVertex();
			}
			angCur = angNext;
		}

		meshBuilder.End();
		pMesh->Draw();
	}
}

void CLightingEditor::UpdateCurrentSelectedAxis( int x, int y )
{
	if ( !IsAnythingSelected() ||
		( GetEditorInteractionMode() != EDITORINTERACTION_TRANSLATE &&
		  GetEditorInteractionMode() != EDITORINTERACTION_ROTATE ) )
	{
		SetCurrentSelectedAxis( EDITORAXIS_NONE );
		return;
	}

	Vector picker = GetPickingRay( x, y );
	Vector origin = GetViewOrigin();
	Vector center = GetSelectionCenter();
	QAngle orientation = GetSelectionOrientation();

	Ray_t pickerRay;
	pickerRay.Init( origin, origin + picker * MAX_TRACE_LENGTH );

	EDITOR_SELECTEDAXIS idealAxis = EDITORAXIS_SCREEN;

	if ( GetEditorInteractionMode() == EDITORINTERACTION_TRANSLATE )
	{
		const float flBoxLength = 24.0f + LIGHT_BOX_SIZE;
		const float flBoxThick = 4.0f;
		Vector boxes[3][2] = {
			{ Vector( LIGHT_BOX_SIZE, -flBoxThick, -flBoxThick ), Vector( flBoxLength, flBoxThick, flBoxThick ) },
			{ Vector( -flBoxThick, LIGHT_BOX_SIZE, -flBoxThick ), Vector( flBoxThick, flBoxLength, flBoxThick ) },
			{ Vector( -flBoxThick, -flBoxThick, LIGHT_BOX_SIZE ), Vector( flBoxThick, flBoxThick, flBoxLength ) },
		};

		float flLastFrac = MAX_TRACE_LENGTH;
		for ( int i = 0; i < 3; i++ )
		{
			CBaseTrace trace;
			if ( !IntersectRayWithBox( pickerRay, center + boxes[i][0], center + boxes[i][1], 0.5f, &trace ) )
				continue;
			if ( trace.fraction > flLastFrac )
				continue;
			flLastFrac = trace.fraction;
			idealAxis = (EDITOR_SELECTEDAXIS)( EDITORAXIS_FIRST + i );
		}

		if ( idealAxis == EDITORAXIS_SCREEN )
		{
			const float flPlaneSize = LIGHT_BOX_SIZE + 12.0f;
			Vector planes[3][2] = {
				{ Vector( 0, 0, -1 ), Vector( flPlaneSize, flPlaneSize, 1 ) },
				{ Vector( 0, -1, 0 ), Vector( flPlaneSize, 1, flPlaneSize ) },
				{ Vector( -1, 0, 0 ), Vector( 1, flPlaneSize, flPlaneSize ) },
			};
			for ( int i = 0; i < 3; i++ )
			{
				CBaseTrace trace;
				if ( !IntersectRayWithBox( pickerRay, center + planes[i][0], center + planes[i][1], 0.5f, &trace ) )
					continue;
				if ( trace.fraction > flLastFrac )
					continue;
				flLastFrac = trace.fraction;
				idealAxis = (EDITOR_SELECTEDAXIS)( EDITORAXIS_FIRST_PLANE + i );
			}
		}
	}
	else
	{
		const int iSubDiv = 32;
		const float flRadius = 32.0f;
		const float flMaxDist = 16.0f;
		const float flRotationStep = 360.0f / iSubDiv;
		float flBestViewDist = MAX_TRACE_LENGTH;

		Vector axes[3];
		AngleVectors( orientation, &axes[0], &axes[1], &axes[2] );
		int iLookup[3][2] = {
			{ 1, 2 },
			{ 2, 1 },
			{ 0, 2 },
		};

		for ( int iDir = 0; iDir < 3; iDir++ )
		{
			float flBestDist = MAX_TRACE_LENGTH;
			QAngle angCur, angNext;
			VectorAngles( axes[ iLookup[iDir][0] ], axes[ iLookup[iDir][1] ], angCur );
			angNext = angCur;

			for ( int iStep = 0; iStep < iSubDiv; iStep++ )
			{
				angNext.z += flRotationStep;
				Vector cUp, nUp;
				AngleVectors( angCur, NULL, NULL, &cUp );
				AngleVectors( angNext, NULL, NULL, &nUp );

				Ray_t ringRay;
				ringRay.Init( center + cUp * flRadius, center + nUp * flRadius );

				float t = 0, s = 0;
				IntersectRayWithRay( pickerRay, ringRay, t, s );

				Vector a = pickerRay.m_Start + pickerRay.m_Delta.Normalized() * t;
				Vector b = ringRay.m_Start + ringRay.m_Delta.Normalized() * s;
				float flDist = ( a - b ).LengthSqr();
				float flStepLengthSqr = ringRay.m_Delta.LengthSqr();

				if ( flDist < flBestDist &&
					flDist < flMaxDist &&
					s * s < flStepLengthSqr &&
					t > 0 && t < ( flBestViewDist - 7 ) )
				{
					flBestDist = flDist;
					flBestViewDist = t;
					idealAxis = (EDITOR_SELECTEDAXIS)( EDITORAXIS_FIRST + iDir );
				}
				angCur = angNext;
			}
		}
	}

	SetCurrentSelectedAxis( idealAxis );
}

void CLightingEditor::AddLightFromScreen( int x, int y )
{
	Vector picker = GetPickingRay( x, y );
	Vector origin = GetViewOrigin();
	trace_t tr;
	CTraceFilterSimple filter( C_BasePlayer::GetLocalPlayer(), COLLISION_GROUP_NONE );
	UTIL_TraceLine( origin, origin + picker * MAX_TRACE_LENGTH, MASK_SOLID, &filter, &tr );
	if ( tr.fraction == 1.0f )
		return;

	DeferredLight_t light = m_AddTemplate;
	light.origin = tr.endpos;
	light.id = 0;
	int id = DeferredLights().Add( light ).id;

	ClearSelection();
	m_Selected.AddToTail( id );
}

bool CLightingEditor::SelectLight( int x, int y, bool bGroup, bool bToggle )
{
	Vector picker = GetPickingRay( x, y );
	Vector origin = GetViewOrigin();

	trace_t tr;
	CTraceFilterWorldOnly filter;
	UTIL_TraceLine( origin, origin + picker * MAX_TRACE_LENGTH, MASK_SOLID, &filter, &tr );

	Vector delta = tr.endpos - tr.startpos;
	const CUtlVector< DeferredLight_t > &lights = DeferredLights().Lights();

	int iBest = -1;
	float flBest = 1.0f;
	for ( int i = 0; i < lights.Count(); i++ )
	{
		CBaseTrace boxtr;
		Vector mins = lights[i].origin - LIGHT_BOX_VEC;
		Vector maxs = lights[i].origin + LIGHT_BOX_VEC;
		if ( !IntersectRayWithBox( tr.startpos, delta, mins, maxs, 0.1f, &boxtr ) )
			continue;
		if ( boxtr.fraction >= flBest )
			continue;
		flBest = boxtr.fraction;
		iBest = i;
	}

	bool bChanged = false;
	if ( iBest == -1 )
	{
		if ( !bGroup && m_Selected.Count() )
		{
			ClearSelection();
			bChanged = true;
		}
		return bChanged;
	}

	int id = lights[iBest].id;
	if ( !bGroup && !bToggle )
	{
		ClearSelection();
		bChanged = true;
	}

	if ( bToggle && IsLightSelected( id ) )
	{
		m_Selected.FindAndRemove( id );
		bChanged = true;
	}
	else if ( !IsLightSelected( id ) )
	{
		m_Selected.AddToTail( id );
		bChanged = true;
	}
	return bChanged;
}

bool CLightingEditor::SelectLights( int *pi2_BoundsMin, int *pi2_BoundsMax, bool bGroup, bool bToggle )
{
	bool bChanged = false;
	if ( !bGroup && !bToggle && m_Selected.Count() )
	{
		ClearSelection();
		bChanged = true;
	}

	const CUtlVector< DeferredLight_t > &lights = DeferredLights().Lights();
	trace_t tr;
	CTraceFilterWorldOnly filter;

	for ( int i = 0; i < lights.Count(); i++ )
	{
		const DeferredLight_t &light = lights[i];
		bool bSelected = IsLightSelected( light.id );
		if ( !bToggle && bSelected )
			continue;

		int sx, sy;
		if ( !GetVectorInScreenSpace( light.origin, sx, sy ) )
			continue;
		if ( sx < pi2_BoundsMin[0] || sy < pi2_BoundsMin[1] || sx > pi2_BoundsMax[0] || sy > pi2_BoundsMax[1] )
			continue;

		UTIL_TraceLine( m_vecEditorView_Origin, light.origin, MASK_SOLID, &filter, &tr );
		if ( tr.fraction < 1.0f && ( tr.endpos - light.origin ).LengthSqr() > ( LIGHT_BOX_SIZE * LIGHT_BOX_SIZE ) )
			continue;

		if ( bSelected )
			m_Selected.FindAndRemove( light.id );
		else
			m_Selected.AddToTail( light.id );
		bChanged = true;
	}
	return bChanged;
}

void CLightingEditor::MoveSelectedLights( const Vector delta )
{
	PurgeMissingSelection();
	FOR_EACH_VEC( m_Selected, i )
		DeferredLights().Find( m_Selected[i] )->origin += delta;
}

void CLightingEditor::RotateSelectedLights( VMatrix matRotate )
{
	PurgeMissingSelection();
	FOR_EACH_VEC( m_Selected, i )
	{
		DeferredLight_t *pLight = DeferredLights().Find( m_Selected[i] );
		VMatrix matCurrent, matDst;
		matCurrent.SetupMatrixOrgAngles( vec3_origin, pLight->angles );
		MatrixMultiply( matRotate, matCurrent, matDst );
		MatrixToAngles( matDst, pLight->angles );
	}

	if ( m_Selected.Count() < 2 )
		return;

	Vector center = GetSelectionCenter();
	FOR_EACH_VEC( m_Selected, i )
	{
		DeferredLight_t *pLight = DeferredLights().Find( m_Selected[i] );
		Vector offset = pLight->origin - center;
		VMatrix matCurrent, matDst;
		matCurrent.SetupMatrixOrgAngles( offset, vec3_angle );
		MatrixMultiply( matRotate, matCurrent, matDst );
		pLight->origin += matDst.GetTranslation() - offset;
	}
}

void CLightingEditor::ClearSelection()
{
	m_Selected.Purge();
}

bool CLightingEditor::IsLightSelected( int id )
{
	return m_Selected.HasElement( id );
}

void CLightingEditor::DeleteSelectedLights()
{
	CUtlVector< int > ids;
	ids.AddVectorToTail( m_Selected );
	ClearSelection();
	FOR_EACH_VEC( ids, i )
		DeferredLights().Remove( ids[i] );
}

void CLightingEditor::SetSelectionCenterLocked( bool locked )
{
	if ( locked )
		m_vecSelectionCenterCache = GetSelectionCenter();
	m_bSelectionCenterLocked = locked;
}

const DeferredLight_t &CLightingEditor::AddTemplate()
{
	return m_AddTemplate;
}

void CLightingEditor::CommitField( EditorLightField_t field, const DeferredLight_t &value )
{
	ApplyLightField( &m_AddTemplate, field, value );
	PurgeMissingSelection();
	FOR_EACH_VEC( m_Selected, i )
		ApplyLightField( DeferredLights().Find( m_Selected[i] ), field, value );
}

void CLightingEditor::SyncTemplateFromSelection()
{
	DeferredLight_t *pLight = FirstSelected();
	if ( !pLight )
		return;
	int id = m_AddTemplate.id;
	m_AddTemplate = *pLight;
	m_AddTemplate.id = id;
}

int CLightingEditor::GetNumSelected()
{
	PurgeMissingSelection();
	return m_Selected.Count();
}

bool CLightingEditor::IsAnythingSelected()
{
	return GetNumSelected() > 0;
}

Vector CLightingEditor::GetSelectionCenter()
{
	if ( m_bSelectionCenterLocked )
		return m_vecSelectionCenterCache;

	PurgeMissingSelection();
	if ( !m_Selected.Count() )
		return vec3_origin;

	Vector mins( FLT_MAX, FLT_MAX, FLT_MAX );
	Vector maxs( -FLT_MAX, -FLT_MAX, -FLT_MAX );
	FOR_EACH_VEC( m_Selected, i )
	{
		Vector origin = DeferredLights().Find( m_Selected[i] )->origin;
		VectorMin( mins, origin, mins );
		VectorMax( maxs, origin, maxs );
	}
	return mins + ( maxs - mins ) * 0.5f;
}

QAngle CLightingEditor::GetSelectionOrientation()
{
	if ( GetNumSelected() != 1 )
		return vec3_angle;
	return FirstSelected()->angles;
}
